#include <fstream>
#include "PostProcessor.h"
#define no_init_all deprecated
#include <d3d11.h>
#include <wrl/client.h>
#define A_CPU
#include "fsr/ffx_a.h"
#include "fsr/ffx_fsr1.h"
#include "Config.h"
#include "shader_fsr_easu.h"
#include "shader_fsr_rcas.h"
#include "VrHooks.h"

using Microsoft::WRL::ComPtr;

namespace vr {
	void CheckResult(const std::string &operation, HRESULT result) {
		if (FAILED(result)) {
			Log() << "Failed (" << std::hex << result << std::dec << "): " << operation << std::endl;
			throw std::exception();
		}
	}
	
	DXGI_FORMAT TranslateTypelessFormats(DXGI_FORMAT format) {
		switch (format) {
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case DXGI_FORMAT_R32G32B32_TYPELESS:
			return DXGI_FORMAT_R32G32B32_FLOAT;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			return DXGI_FORMAT_R10G10B10A2_UINT;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			return DXGI_FORMAT_B8G8R8A8_UNORM;

		default:
			return format;
		}
	}

	DXGI_FORMAT MakeSrgbFormatsTypeless(DXGI_FORMAT format) {
		switch (format) {
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			return DXGI_FORMAT_B8G8R8A8_TYPELESS;
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return DXGI_FORMAT_B8G8R8X8_TYPELESS;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		default:
			return format;
		}
	}

	bool IsConsideredSrgbByOpenVR(DXGI_FORMAT format) {
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return true;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			// OpenVR appears to treat submitted typeless textures as SRGB
			return true;
		default:
			return false;
		}
	}

	bool IsSrgbFormat(DXGI_FORMAT format) {
		switch (format) {
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	}

	void CalculateProjectionCenter(EVREye eye, float &x, float &y) {
		IVRSystem *vrSystem = (IVRSystem*) VR_GetGenericInterface(IVRSystem_Version, nullptr);
		float left, right, top, bottom;
		vrSystem->GetProjectionRaw(eye, &left, &right, &top, &bottom);
		Log() << "Raw projection for eye " << eye << ": l " << left << ", r " << right << ", t " << top << ", b " << bottom << "\n";

		// calculate canted angle between the eyes
		auto ml = vrSystem->GetEyeToHeadTransform(Eye_Left);
		auto mr = vrSystem->GetEyeToHeadTransform(Eye_Right);
		float dotForward = ml.m[2][0] * mr.m[2][0] + ml.m[2][1] * mr.m[2][1] + ml.m[2][2] * mr.m[2][2];
		float cantedAngle = std::abs(std::acosf(dotForward) / 2) * (eye == Eye_Right ? -1 : 1);
		Log() << "Display is canted by " << cantedAngle << " RAD\n";

		float canted = std::tanf(cantedAngle);
		x = 0.5f * (1.f + (right + left - 2*canted) / (left - right));
		y = 0.5f * (1.f + (bottom + top) / (top - bottom));
		Log() << "Projection center for eye " << eye << ": " << x << ", " << y << "\n";
	}

	void PostProcessor::Apply(EVREye eEye, const Texture_t *pTexture, const VRTextureBounds_t* pBounds, EVRSubmitFlags nSubmitFlags) {
		if (!enabled || pTexture == nullptr || pTexture->eType != TextureType_DirectX || pTexture->handle == nullptr) {
			return;
		}

		static VRTextureBounds_t defaultBounds { 0, 0, 1, 1 };
		if (pBounds == nullptr) {
			pBounds = &defaultBounds;
		}

		ID3D11Texture2D *texture = (ID3D11Texture2D*)pTexture->handle;

		if ( Config::Instance().fsrEnabled ) {
			if (initialized) {
				D3D11_TEXTURE2D_DESC td;
				texture->GetDesc(&td);
				if (td.Width != inputWidth || td.Height != inputHeight) {
					Log() << "Texture size changed, recreating resources...\n";
					Reset();
				}
			}
			if (!initialized) {
				try {
					textureContainsOnlyOneEye = std::abs(pBounds->uMax - pBounds->uMin) > .5f;
					PrepareResources(texture, pTexture->eColorSpace);
				} catch (...) {
					Log() << "Resource creation failed, disabling\n";
					enabled = false;
					return;
				}
			}

			// if a single shared texture is used for both eyes, only apply effects on the first Submit
			if (eyeCount == 0 || textureContainsOnlyOneEye || texture != lastSubmittedTexture) {
				ApplyPostProcess(textureContainsOnlyOneEye ? eEye : Eye_Left, texture);
			}
			lastSubmittedTexture = texture;
			eyeCount = (eyeCount + 1) % 2;
			const_cast<Texture_t*>(pTexture)->handle = outputTexture;
			const_cast<Texture_t*>(pTexture)->eColorSpace = inputIsSrgb ? ColorSpace_Gamma : ColorSpace_Auto;
		}
	}

	void PostProcessor::Reset() {
		enabled = true;
		initialized = false;
		device.Reset();
		context.Reset();
		sampler.Reset();
		inputTextureViews.clear();
		copiedTexture.Reset();
		copiedTextureView.Reset();
		upscaleShader.Reset();
		upscaleConstantsBuffer[0].Reset();
		upscaleConstantsBuffer[1].Reset();
		upscaledTexture.Reset();
		upscaledTextureUav.Reset();
		upscaledTextureView.Reset();
		rCASShader.Reset();
		sharpenConstantsBuffer[0].Reset();
		sharpenConstantsBuffer[1].Reset();
		sharpenedTexture.Reset();
		sharpenedTextureUav.Reset();
		lastSubmittedTexture = nullptr;
		outputTexture = nullptr;
		eyeCount = 0;
		for (int i = 0; i < QUERY_COUNT; ++i) {
			profileQueries[i].queryStart.Reset();
			profileQueries[i].queryEnd.Reset();
			profileQueries[i].queryDisjoint.Reset();
		}
	}

	void PostProcessor::PrepareCopyResources( DXGI_FORMAT format ) {
		Log() << "Creating copy texture of size " << inputWidth << "x" << inputHeight << "\n";
		D3D11_TEXTURE2D_DESC td;
		td.Width = inputWidth;
		td.Height = inputHeight;
		td.MipLevels = 1;
		td.CPUAccessFlags = 0;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		td.Format = MakeSrgbFormatsTypeless(format);
		td.MiscFlags = 0;
		td.SampleDesc.Count = 1;
		td.SampleDesc.Quality = 0;
		td.ArraySize = 1;
		CheckResult("Creating copy texture", device->CreateTexture2D( &td, nullptr, copiedTexture.GetAddressOf()));
		D3D11_SHADER_RESOURCE_VIEW_DESC srv;
		srv.Format = TranslateTypelessFormats(td.Format);
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		srv.Texture2D.MostDetailedMip = 0;
		CheckResult("Creating copy SRV", device->CreateShaderResourceView(copiedTexture.Get(), &srv, copiedTextureView.GetAddressOf()));
	}

	ID3D11ShaderResourceView * PostProcessor::GetInputView( ID3D11Texture2D *inputTexture ) {
		if (requiresCopy) {
			D3D11_TEXTURE2D_DESC td;
			inputTexture->GetDesc(&td);
			if (td.SampleDesc.Count > 1) {
				context->ResolveSubresource(copiedTexture.Get(), 0, inputTexture, 0, td.Format);
			} else {
				D3D11_BOX region;
				region.left = region.top = region.front = 0;
				region.right = td.Width;
				region.bottom = td.Height;
				region.back = 1;
				context->CopySubresourceRegion(copiedTexture.Get(), 0, 0, 0, 0, inputTexture, 0, &region);
			}
			return copiedTextureView.Get();
		}
		
		if (inputTextureViews.find(inputTexture) == inputTextureViews.end()) {
			Log() << "Creating shader resource view for input texture " << inputTexture << std::endl;
			// create resource view for input texture
			D3D11_TEXTURE2D_DESC std;
			inputTexture->GetDesc( &std );
			Log() << "Texture has size " << std.Width << "x" << std.Height << " and format " << std.Format << "\n";
			D3D11_SHADER_RESOURCE_VIEW_DESC svd;
			svd.Format = TranslateTypelessFormats(std.Format);
			svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			svd.Texture2D.MostDetailedMip = 0;
			svd.Texture2D.MipLevels = 1;
			HRESULT result = device->CreateShaderResourceView( inputTexture, &svd, inputTextureViews[inputTexture].GetAddressOf() );
			if (FAILED(result)) {
				Log() << "Failed to create resource view: " << std::hex << (unsigned long)result << std::dec << std::endl;
				inputTextureViews.erase( inputTexture );
				return nullptr;
			}
		}
		return inputTextureViews[inputTexture].Get();
	}

	struct UpscaleConstants {
		AU1 const0[4];
		AU1 const1[4];
		AU1 const2[4];
		AU1 const3[4];
		AU1 imageCentre[4];
		AU1 radius[4];
	};
	
	void PostProcessor::PrepareUpscalingResources() {
		CheckResult("Creating FSR upscale shader", device->CreateComputeShader( g_FSRUpscaleShader, sizeof(g_FSRUpscaleShader), nullptr, upscaleShader.GetAddressOf()));

		float proj[4];
		CalculateProjectionCenter(Eye_Left, proj[0], proj[1]);
		CalculateProjectionCenter(Eye_Right, proj[2], proj[3]);
		UpscaleConstants constants;
		// create shader constants buffers
		FsrEasuCon(constants.const0, constants.const1, constants.const2, constants.const3, inputWidth, inputHeight, inputWidth, inputHeight, outputWidth, outputHeight);
		constants.imageCentre[0] = textureContainsOnlyOneEye ? outputWidth * proj[0] : outputWidth / 2 * proj[0];
		constants.imageCentre[1] = outputHeight * proj[1];
		constants.imageCentre[2] = textureContainsOnlyOneEye ? outputWidth * proj[0] : outputWidth / 2 * (1 + proj[2]);
		constants.imageCentre[3] = outputHeight * (textureContainsOnlyOneEye ? proj[1] : proj[3]);
		constants.radius[0] = 0.5f * Config::Instance().radius * outputHeight;
		constants.radius[1] = constants.radius[0] * constants.radius[0];
		constants.radius[2] = outputWidth;
		constants.radius[3] = outputHeight;
		D3D11_BUFFER_DESC bd;
		bd.Usage = D3D11_USAGE_IMMUTABLE;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = 0;
		bd.MiscFlags = 0;
		bd.StructureByteStride = 0;
		bd.ByteWidth = sizeof(UpscaleConstants);
		D3D11_SUBRESOURCE_DATA init;
		init.SysMemPitch = 0;
		init.SysMemSlicePitch = 0;
		init.pSysMem = &constants;
		CheckResult("Creating FSR constants buffer", device->CreateBuffer( &bd, &init, upscaleConstantsBuffer[0].GetAddressOf()));
		if (textureContainsOnlyOneEye) {
			constants.imageCentre[0] = outputWidth * proj[2];
			constants.imageCentre[1] = outputHeight * proj[3];
			constants.imageCentre[2] = outputWidth * proj[2];
			constants.imageCentre[3] = outputHeight * proj[3];
			CheckResult("Creating FSR constants buffer", device->CreateBuffer( &bd, &init, upscaleConstantsBuffer[1].GetAddressOf()));
		}

		Log() << "Creating upscaled texture of size " << outputWidth << "x" << outputHeight << "\n";
		D3D11_TEXTURE2D_DESC td;
		td.Width = outputWidth;
		td.Height = outputHeight;
		td.MipLevels = 1;
		td.CPUAccessFlags = 0;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.MiscFlags = 0;
		td.SampleDesc.Count = 1;
		td.SampleDesc.Quality = 0;
		td.ArraySize = 1;
		CheckResult("Creating upscaled texture", device->CreateTexture2D( &td, nullptr, upscaledTexture.GetAddressOf()));
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav;
		uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uav.Texture2D.MipSlice = 0;
		CheckResult("Creating upscaled UAV", device->CreateUnorderedAccessView( upscaledTexture.Get(), &uav, upscaledTextureUav.GetAddressOf()));
		D3D11_SHADER_RESOURCE_VIEW_DESC srv;
		srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		srv.Texture2D.MostDetailedMip = 0;
		CheckResult("Creating upscaled SRV", device->CreateShaderResourceView(upscaledTexture.Get(), &srv, upscaledTextureView.GetAddressOf()));
	}

	void PostProcessor::ApplyUpscaling( EVREye eEye, ID3D11ShaderResourceView *inputView ) {
		UINT uavCount = -1;
		context->CSSetUnorderedAccessViews( 0, 1, upscaledTextureUav.GetAddressOf(), &uavCount );
		context->CSSetConstantBuffers( 0, 1, upscaleConstantsBuffer[eEye].GetAddressOf() );
		ID3D11ShaderResourceView *srvs[1] = {inputView};
		context->CSSetShaderResources( 0, 1, srvs );
		context->CSSetShader( upscaleShader.Get(), nullptr, 0 );
		context->CSSetSamplers( 0, 1, sampler.GetAddressOf() );
		context->Dispatch( (outputWidth+15)>>4, (outputHeight+15)>>4, 1 );
	}

	struct SharpenConstants {
		AU1 const0[4];
		AU1 imageCentre[4];
		AU1 radius[4];
	};

	void PostProcessor::PrepareSharpeningResources() {
		CheckResult("Creating rCAS sharpening shader", device->CreateComputeShader( g_FSRSharpenShader, sizeof(g_FSRSharpenShader), nullptr, rCASShader.GetAddressOf()));

		float proj[4];
		CalculateProjectionCenter(Eye_Left, proj[0], proj[1]);
		CalculateProjectionCenter(Eye_Right, proj[2], proj[3]);
		SharpenConstants constants;
		float sharpness = AClampF1( Config::Instance().sharpness, 0, 1 );
		FsrRcasCon(constants.const0, 2.f - 2*sharpness);
		constants.imageCentre[0] = textureContainsOnlyOneEye ? outputWidth * proj[0] : outputWidth / 2 * proj[0];
		constants.imageCentre[1] = outputHeight * proj[1];
		constants.imageCentre[2] = textureContainsOnlyOneEye ? outputWidth * proj[0] : outputWidth / 2 * (1 + proj[2]);
		constants.imageCentre[3] = outputHeight * (textureContainsOnlyOneEye ? proj[1] : proj[3]);
		constants.radius[0] = 0.5f * Config::Instance().radius * outputHeight;
		constants.radius[1] = constants.radius[0] * constants.radius[0];
		constants.radius[2] = outputWidth;
		constants.radius[3] = outputHeight;
		constants.const0[3] = Config::Instance().debugMode;
		D3D11_BUFFER_DESC bd;
		bd.Usage = D3D11_USAGE_IMMUTABLE;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = 0;
		bd.MiscFlags = 0;
		bd.StructureByteStride = 0;
		bd.ByteWidth = sizeof(SharpenConstants);
		D3D11_SUBRESOURCE_DATA init;
		init.SysMemPitch = 0;
		init.SysMemSlicePitch = 0;
		init.pSysMem = &constants;
		CheckResult("Creating rCAS constants buffer", device->CreateBuffer( &bd, &init, sharpenConstantsBuffer[0].GetAddressOf()));
		if (textureContainsOnlyOneEye) {
			constants.imageCentre[0] = outputWidth * proj[2];
			constants.imageCentre[1] = outputHeight * proj[3];
			constants.imageCentre[2] = outputWidth * proj[2];
			constants.imageCentre[3] = outputHeight * proj[3];
			CheckResult("Creating rCAS constants buffer", device->CreateBuffer( &bd, &init, sharpenConstantsBuffer[1].GetAddressOf()));
		}
		
		Log() << "Creating sharpened texture of size " << outputWidth << "x" << outputHeight << "\n";
		D3D11_TEXTURE2D_DESC td;
		td.Width = outputWidth;
		td.Height = outputHeight;
		td.MipLevels = 1;
		td.CPUAccessFlags = 0;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.MiscFlags = 0;
		td.SampleDesc.Count = 1;
		td.SampleDesc.Quality = 0;
		td.ArraySize = 1;
		CheckResult("Creating sharpened texture", device->CreateTexture2D( &td, nullptr, sharpenedTexture.GetAddressOf()));
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav;
		uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uav.Texture2D.MipSlice = 0;
		CheckResult("Creating sharpened UAV", device->CreateUnorderedAccessView( sharpenedTexture.Get(), &uav, sharpenedTextureUav.GetAddressOf()));
	}

	void PostProcessor::ApplySharpening( EVREye eEye, ID3D11ShaderResourceView *inputView ) {
		UINT uavCount = -1;
		context->CSSetUnorderedAccessViews( 0, 1, sharpenedTextureUav.GetAddressOf(), &uavCount );
		context->CSSetConstantBuffers( 0, 1, sharpenConstantsBuffer[eEye].GetAddressOf() );
		ID3D11ShaderResourceView *srvs[1] = {inputView};
		context->CSSetShaderResources( 0, 1, srvs );
		context->CSSetSamplers( 0, 1, sampler.GetAddressOf() );
		context->CSSetShader( rCASShader.Get(), nullptr, 0 );
		context->Dispatch( (outputWidth+15)>>4, (outputHeight+15)>>4, 1 );
	}

	void PostProcessor::PrepareResources( ID3D11Texture2D *inputTexture, EColorSpace colorSpace ) {
		Log() << "Creating post-processing resources\n";
		inputTexture->GetDevice( device.GetAddressOf() );
		device->GetImmediateContext( context.GetAddressOf() );
		D3D11_TEXTURE2D_DESC std;
		inputTexture->GetDesc( &std );
		inputIsSrgb = colorSpace == ColorSpace_Gamma || (colorSpace == ColorSpace_Auto && IsConsideredSrgbByOpenVR(std.Format));
		if (inputIsSrgb) {
			Log() << "Input texture is in SRGB color space\n";
		}

		inputWidth = std.Width;
		inputHeight = std.Height;

		if ( Config::Instance().renderScale < 1.f) {
			outputWidth = std.Width / Config::Instance().renderScale;
			outputHeight = std.Height / Config::Instance().renderScale;
		} else {
			outputWidth = std.Width * Config::Instance().renderScale;
			outputHeight = std.Height * Config::Instance().renderScale;
		}

		if (!(std.BindFlags & D3D11_BIND_SHADER_RESOURCE) || std.SampleDesc.Count > 1 || IsSrgbFormat(std.Format)) {
			Log() << "Input texture can't be bound directly, need to copy\n";
			requiresCopy = true;
			PrepareCopyResources(std.Format);
		}

		if (Config::Instance().fsrEnabled) {
			if (Config::Instance().renderScale != 1) {
				PrepareUpscalingResources();
			}
			PrepareSharpeningResources();

			if (Config::Instance().applyMIPBias) {
				float mipLodBias = -log2(outputWidth / (float)inputWidth);
				HookD3D11Context(context.Get(), device.Get(), mipLodBias);
				// ensure that all currently set samplers get LOD bias applied, even if the engine
				// never changes them again
				ID3D11SamplerState *samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
				context->PSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, samplers);
				context->PSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, samplers);
			}

			if (Config::Instance().debugMode) {
				for (int i = 0; i < QUERY_COUNT; ++i) {
					D3D11_QUERY_DESC qd;
					qd.Query = D3D11_QUERY_TIMESTAMP;
					qd.MiscFlags = 0;
					device->CreateQuery(&qd, profileQueries[i].queryStart.GetAddressOf());
					device->CreateQuery(&qd, profileQueries[i].queryEnd.GetAddressOf());
					qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
					device->CreateQuery(&qd, profileQueries[i].queryDisjoint.GetAddressOf());
				}
			}
		}

		initialized = true;
	}

	void PostProcessor::ApplyPostProcess( EVREye eEye, ID3D11Texture2D *inputTexture ) {
		ID3D11Buffer* currentConstBuffs[1];
		ID3D11ShaderResourceView* currentSRVs[1];
		ID3D11UnorderedAccessView* currentUAVs[1];

		context->CSGetShaderResources(0, 1, currentSRVs);
		context->CSGetUnorderedAccessViews(0, 1, currentUAVs);
		context->CSGetConstantBuffers(0, 1, currentConstBuffs);

		outputTexture = inputTexture;

		ID3D11ShaderResourceView *inputView = GetInputView(inputTexture);
		if (inputView == nullptr) {
			return;
		}

		if (Config::Instance().debugMode) {
			context->Begin(profileQueries[currentQuery].queryDisjoint.Get());
			context->End(profileQueries[currentQuery].queryStart.Get());
		}

		context->OMSetRenderTargets(0, nullptr, nullptr);

		if (Config::Instance().fsrEnabled && Config::Instance().renderScale != 1) {
			ApplyUpscaling(eEye, inputView);
			inputView = upscaledTextureView.Get();
			outputTexture = upscaledTexture.Get();
		}
		if (Config::Instance().fsrEnabled) {
			ApplySharpening(eEye, inputView);
			outputTexture = sharpenedTexture.Get();
		}

		context->CSSetShaderResources(0, 1, currentSRVs);
		UINT uavCount = -1;
		context->CSSetUnorderedAccessViews(0, 1, currentUAVs, &uavCount);
		context->CSSetConstantBuffers(0, 1, currentConstBuffs);

		if (Config::Instance().debugMode) {
			context->End(profileQueries[currentQuery].queryEnd.Get());
			context->End(profileQueries[currentQuery].queryDisjoint.Get());

			currentQuery = (currentQuery + 1) % QUERY_COUNT;
			while (context->GetData(profileQueries[currentQuery].queryDisjoint.Get(), nullptr, 0, 0) == S_FALSE) {
				Sleep(1);
			}
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
			HRESULT result = context->GetData(profileQueries[currentQuery].queryDisjoint.Get(), &disjoint, sizeof(disjoint), 0);
			if (result == S_OK && !disjoint.Disjoint) {
				UINT64 begin, end;
				context->GetData(profileQueries[currentQuery].queryStart.Get(), &begin, sizeof(UINT64), 0);
				context->GetData(profileQueries[currentQuery].queryEnd.Get(), &end, sizeof(UINT64), 0);
				float duration = (end - begin) / float(disjoint.Frequency);
				summedGpuTime += duration;
				++countedQueries;

				if (countedQueries >= 500) {
					float avgTimeMs = 1000.f / countedQueries * summedGpuTime;
					if (textureContainsOnlyOneEye)
						avgTimeMs *= 2;
					Log() << "Average GPU processing time for FSR application: " << avgTimeMs << " ms\n";
					countedQueries = 0;
					summedGpuTime = 0.f;
				}
			}
		}
	}
}
