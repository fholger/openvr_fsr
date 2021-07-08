#include <fstream>
#include "PostProcessor.h"
#define no_init_all deprecated
#include <d3d11.h>
#include <wrl/client.h>
#include "fsr/fsr_up.h"
#include "fsr/fsr_cas.h"
#include "Config.h"

using Microsoft::WRL::ComPtr;

namespace vr {
	void CheckResult(const std::string &operation, HRESULT result) {
		if (FAILED(result)) {
			Log() << "Failed (" << std::hex << result << std::dec << "): " << operation << std::endl;
			throw std::exception();
		}
	}
	
	DXGI_FORMAT TranslateTypelessFormats(DXGI_FORMAT format) {
		Log() << "Mapping format " << std::hex << format << std::dec << std::endl;
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

	bool IsSrgbFormat(DXGI_FORMAT format) {
		return format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	}

	void PostProcessor::Apply(EVREye eEye, const Texture_t *pTexture, const VRTextureBounds_t* pBounds, EVRSubmitFlags nSubmitFlags) {
		if (!enabled || pTexture->eType != TextureType_DirectX || pTexture->handle == nullptr) {
			return;
		}

		ID3D11Texture2D *texture = (ID3D11Texture2D*)pTexture->handle;

		if ( Config::Instance().fsrEnabled ) {
			if (!initialized) {
				try {
					PrepareResources(texture, pTexture->eColorSpace);
				} catch (...) {
					Log() << "Resource creation failed, disabling\n";
					enabled = false;
				}
			}

			// if a single texture is used for both eyes, only apply effects on the first Submit
			if (eyeCount == 0 || texture != lastSubmittedTexture) {
				ApplyPostProcess(texture);
			}
			lastSubmittedTexture = texture;
			eyeCount = (eyeCount + 1) % 2;
			const_cast<Texture_t*>(pTexture)->handle = outputTexture;
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
		upscaleConstantsBuffer.Reset();
		upscaledTexture.Reset();
		upscaledTextureUav.Reset();
		upscaledTextureView.Reset();
		rCASShader.Reset();
		sharpenConstantsBuffer.Reset();
		sharpenedTexture.Reset();
		sharpenedTextureUav.Reset();
		lastSubmittedTexture = nullptr;
		outputTexture = nullptr;
		eyeCount = 0;
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
		td.Format = format;
		td.MiscFlags = 0;
		td.SampleDesc.Count = 1;
		td.SampleDesc.Quality = 0;
		td.ArraySize = 1;
		CheckResult("Creating copy texture", device->CreateTexture2D( &td, nullptr, copiedTexture.GetAddressOf()));
		D3D11_SHADER_RESOURCE_VIEW_DESC srv;
		srv.Format = TranslateTypelessFormats(format);
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
			Log() << "Texture has size " << std.Width << "x" << std.Height << "\n";
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
		float targetWidth;
		float targetHeight;
		float invTargetWidth;
		float invTargetHeight;
		float sourceWidth;
		float sourceHeight;
		float padding[2];
	};
	
	void PostProcessor::PrepareUpscalingResources() {
		CheckResult("Creating FSR upscale shader", device->CreateComputeShader( fsr_up_dxbc, fsr_up_dxbc_len, nullptr, upscaleShader.GetAddressOf()));

		UpscaleConstants constants;
		// create shader constants buffers
		constants.targetWidth = outputWidth;
		constants.targetHeight = outputHeight;
		constants.invTargetWidth = 1.f / outputWidth;
		constants.invTargetHeight = 1.f / outputHeight;
		constants.sourceWidth = inputWidth;
		constants.sourceHeight = inputHeight;
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
		CheckResult("Creating FSR constants buffer", device->CreateBuffer( &bd, &init, upscaleConstantsBuffer.GetAddressOf()));

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

	void PostProcessor::ApplyUpscaling( ID3D11ShaderResourceView *inputView ) {
		UINT uavCount = -1;
		context->CSSetUnorderedAccessViews( 0, 1, upscaledTextureUav.GetAddressOf(), &uavCount );
		context->CSSetConstantBuffers( 0, 1, upscaleConstantsBuffer.GetAddressOf() );
		ID3D11ShaderResourceView *srvs[1] = {inputView};
		context->CSSetShaderResources( 0, 1, srvs );
		context->CSSetShader( upscaleShader.Get(), nullptr, 0 );
		context->CSSetSamplers( 0, 1, sampler.GetAddressOf() );
		context->Dispatch( (outputWidth+15)>>4, (outputHeight+15)>>4, 1 );
	}

	struct SharpenConstants {
		float sharpness;
		float padding[3];
	};

	void PostProcessor::PrepareSharpeningResources() {
		CheckResult("Creating rCAS sharpening shader", device->CreateComputeShader( fsr_cas_dxbc, fsr_cas_dxbc_len, nullptr, rCASShader.GetAddressOf()));

		SharpenConstants constants;
		constants.sharpness = -std::log2(Config::Instance().sharpness);
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
		CheckResult("Creating rCAS constants buffer", device->CreateBuffer( &bd, &init, sharpenConstantsBuffer.GetAddressOf()));
		
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

	void PostProcessor::ApplySharpening( ID3D11ShaderResourceView *inputView ) {
		UINT uavCount = -1;
		context->CSSetUnorderedAccessViews( 0, 1, sharpenedTextureUav.GetAddressOf(), &uavCount );
		context->CSSetConstantBuffers( 0, 1, sharpenConstantsBuffer.GetAddressOf() );
		ID3D11ShaderResourceView *srvs[1] = {inputView};
		context->CSSetShaderResources( 0, 1, srvs );
		context->CSSetShader( rCASShader.Get(), nullptr, 0 );
		context->Dispatch( (outputWidth+15)>>4, (outputHeight+15)>>4, 1 );
	}

	void PostProcessor::PrepareResources( ID3D11Texture2D *inputTexture, EColorSpace colorSpace ) {
		Log() << "Creating post-processing resources\n";
		inputTexture->GetDevice( device.GetAddressOf() );
		device->GetImmediateContext( context.GetAddressOf() );
		D3D11_TEXTURE2D_DESC std;
		inputTexture->GetDesc( &std );
		inputIsSrgb = colorSpace == ColorSpace_Gamma || (colorSpace == ColorSpace_Auto && IsSrgbFormat(std.Format));
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

		if (!(std.BindFlags & D3D11_BIND_SHADER_RESOURCE) || std.SampleDesc.Count > 1) {
			Log() << "Input texture can't be bound, need to copy\n";
			requiresCopy = true;
			PrepareCopyResources(std.Format);
		}

		if (Config::Instance().fsrEnabled) {
			if (Config::Instance().renderScale != 1) {
				PrepareUpscalingResources();
			}
			PrepareSharpeningResources();
		}

		initialized = true;
	}

	void PostProcessor::ApplyPostProcess( ID3D11Texture2D *inputTexture ) {
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

		context->OMSetRenderTargets(0, nullptr, nullptr);

		if (Config::Instance().fsrEnabled && Config::Instance().renderScale != 1) {
			ApplyUpscaling(inputView);
			inputView = upscaledTextureView.Get();
			outputTexture = upscaledTexture.Get();
		}
		if (Config::Instance().fsrEnabled) {
			ApplySharpening(inputView);
			outputTexture = sharpenedTexture.Get();
		}

		context->CSSetShaderResources(0, 1, currentSRVs);
		UINT uavCount = -1;
		context->CSSetUnorderedAccessViews(0, 1, currentUAVs, &uavCount);
		context->CSSetConstantBuffers(0, 1, currentConstBuffs);
	}
}
