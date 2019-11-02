#include <fstream>
#include <unordered_map>
#include "WrappedIVRCompositor.h"
#define no_init_all deprecated
#include <d3d11.h>
#include <iostream>
#include <wrl/client.h>
#include "fsr/fsr_up.h"
#include "fsr/fsr_cas.h"
#include <stdint.h>
#include "Config.h"

using Microsoft::WRL::ComPtr;

namespace {
	void helper() {}
}

namespace vr {
	std::string GetDllPath() {
		char path[FILENAME_MAX];
		HMODULE hm = nullptr;

		void *address = helper;		

		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address, &hm)) {
			return ".";
		}

		GetModuleFileNameA(hm, path, sizeof(path));

		std::string p = path;
		return p.substr(0, p.find_last_of('\\'));
	}

	std::ostream& log() {
		try {
			static std::ofstream logFile (GetDllPath() + "\\openvr_mod.log");
			return logFile;
		} catch (...) {
			return std::cout;
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

		default:
			return format;
		}
	}

	struct WrappedIVRCompositor::FSRRenderResources {
		bool created = false;
		bool enabled = true;
		DWORD lastSwitch = 0;
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11Texture2D> upscaledTexture;
		ComPtr<ID3D11Texture2D> outputTexture;
		ComPtr<ID3D11ComputeShader> fsrUpscaleShader;
		ComPtr<ID3D11ComputeShader> fsrSharpenShader;
		std::unordered_map<ID3D11Texture2D*, ComPtr<ID3D11ShaderResourceView>> inputTextureViews;
		ComPtr<ID3D11UnorderedAccessView> upscaledTextureUav;
		ComPtr<ID3D11ShaderResourceView> upscaledTextureView;
		ComPtr<ID3D11UnorderedAccessView> outputTextureUav;
		ComPtr<ID3D11Buffer> upscaleConstantsBuffer;
		ComPtr<ID3D11Buffer> sharpenConstantsBuffer;
		ComPtr<ID3D11SamplerState> samplerState;
		uint32_t outputWidth, outputHeight;
		struct UpscaleConstants {
			float targetSize[4];
			float sourceSize[4];
		} upscaleConstants;
		struct SharpenConstants {
			float sharpness[4];
		} sharpenConstants;

		void Create(ID3D11Texture2D *tex) {
			log() << "Creating resources for FSR\n";
			tex->GetDevice( device.GetAddressOf() );
			device->GetImmediateContext( context.GetAddressOf() );
			D3D11_TEXTURE2D_DESC std;
			tex->GetDesc( &std );

			// create output textures
			if ( Config::Instance().fsrQuality < 1.f) {
				outputWidth = std.Width / Config::Instance().fsrQuality;
				outputHeight = std.Height / Config::Instance().fsrQuality;
			} else {
				outputWidth = std.Width * Config::Instance().fsrQuality;
				outputHeight = std.Height * Config::Instance().fsrQuality;
			}
			log() << "Creating output texture of size " << outputWidth << "x" << outputHeight << "\n";
			D3D11_TEXTURE2D_DESC td;
			td.Width = outputWidth;
			td.Height = outputHeight;
			td.MipLevels = 1;
			td.CPUAccessFlags = 0;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.MiscFlags = 0;
			td.SampleDesc.Count = 1;
			td.SampleDesc.Quality = 0;
			td.ArraySize = 1;
			if (FAILED(device->CreateTexture2D( &td, nullptr, upscaledTexture.GetAddressOf() ))) {
				log() << "Failed to create texture.\n";
				return;
			}
			if (FAILED(device->CreateTexture2D( &td, nullptr, outputTexture.GetAddressOf() ))) {
				log() << "Failed to create texture.\n";
				return;
			}
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav;
			uav.Format = td.Format;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uav.Texture2D.MipSlice = 0;
			device->CreateUnorderedAccessView( upscaledTexture.Get(), &uav, upscaledTextureUav.GetAddressOf() );
			device->CreateUnorderedAccessView( outputTexture.Get(), &uav, outputTextureUav.GetAddressOf() );
			D3D11_SHADER_RESOURCE_VIEW_DESC srv;
			srv.Format = td.Format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = 1;
			srv.Texture2D.MostDetailedMip = 0;
			device->CreateShaderResourceView(upscaledTexture.Get(), &srv, upscaledTextureView.GetAddressOf());

			// create shaders
			if (FAILED(device->CreateComputeShader( fsr_up_dxbc, fsr_up_dxbc_len, nullptr, fsrUpscaleShader.GetAddressOf() ))) {
				log() << "Failed to create compute shader.\n";
				return;
			}
			if (FAILED(device->CreateComputeShader( fsr_cas_dxbc, fsr_cas_dxbc_len, nullptr, fsrSharpenShader.GetAddressOf() ))) {
				log() << "Failed to create compute shader.\n";
				return;
			}

			D3D11_SAMPLER_DESC sd;
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.MipLODBias = 0;
			sd.MinLOD = 0;
			sd.MaxLOD = 0;
			sd.MaxAnisotropy = 1;
			device->CreateSamplerState( &sd, samplerState.GetAddressOf() );

			// create shader constants buffers
			upscaleConstants.targetSize[0] = outputWidth;
			upscaleConstants.targetSize[1] = outputHeight;
			upscaleConstants.targetSize[2] = 1.f / outputWidth;
			upscaleConstants.targetSize[3] = 1.f / outputHeight;
			upscaleConstants.sourceSize[0] = std.Width;
			upscaleConstants.sourceSize[1] = std.Height;
			upscaleConstants.sourceSize[2] = 1;
			upscaleConstants.sourceSize[3] = 1;
			sharpenConstants.sharpness[0] = Config::Instance().sharpness;
			sharpenConstants.sharpness[1] = 1;
			sharpenConstants.sharpness[2] = 1;
			sharpenConstants.sharpness[3] = 1;
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
			init.pSysMem = &upscaleConstants;
			if (FAILED(device->CreateBuffer( &bd, &init, upscaleConstantsBuffer.GetAddressOf() ))) {
				log() << "Failed to create upscale shader constants buffer.\n";
				return;
			}
			bd.ByteWidth = sizeof(SharpenConstants);
			init.pSysMem = &sharpenConstants;
			if (FAILED(device->CreateBuffer( &bd, &init, sharpenConstantsBuffer.GetAddressOf() ))) {
				log() << "Failed to create sharpen shader constants buffer.\n";
				return;
			}

			log() << "Resource creation complete\n";
			created = true;
		}

		void Apply(ID3D11Texture2D *tex) {
			if (inputTextureViews.find(tex) == inputTextureViews.end()) {
				log() << "Creating shader resource view for input texture " << tex << "\n";
				// create resource view for input texture
				D3D11_TEXTURE2D_DESC std;
				tex->GetDesc( &std );
				D3D11_SHADER_RESOURCE_VIEW_DESC svd;
				svd.Format = TranslateTypelessFormats(std.Format);
				svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				svd.Texture2D.MostDetailedMip = 0;
				svd.Texture2D.MipLevels = 1;
				HRESULT result = device->CreateShaderResourceView( tex, &svd, inputTextureViews[tex].GetAddressOf() );
				if (FAILED(result)) {
					log() << "Failed to create resource view: " << std::hex << (unsigned long)result << std::endl;
					inputTextureViews.erase( tex );
					return;
				}
			}

			context->OMSetRenderTargets( 0, nullptr, nullptr );
			UINT uavCount = -1;
			context->CSSetUnorderedAccessViews( 0, 1, upscaledTextureUav.GetAddressOf(), &uavCount );
			context->CSSetConstantBuffers( 0, 1, upscaleConstantsBuffer.GetAddressOf() );
			context->CSSetShaderResources( 0, 1, inputTextureViews[tex].GetAddressOf() );
			context->CSSetShader( fsrUpscaleShader.Get(), nullptr, 0 );
			context->CSSetSamplers( 0, 1, samplerState.GetAddressOf() );
			context->Dispatch( (outputWidth+15)>>4, (outputHeight+15)>>4, 1 );
			context->CSSetUnorderedAccessViews( 0, 1, outputTextureUav.GetAddressOf(), &uavCount );
			context->CSSetConstantBuffers( 0, 1, sharpenConstantsBuffer.GetAddressOf() );
			context->CSSetShaderResources( 0, 1, upscaledTextureView.GetAddressOf() );
			context->CSSetShader( fsrSharpenShader.Get(), nullptr, 0 );
			context->Dispatch( (outputWidth+15)>>4, (outputHeight+15)>>4, 1 );
			context->CSSetShaderResources( 0, 0, nullptr );
			context->CSSetUnorderedAccessViews( 0, 0, nullptr, nullptr );
			context->CSSetConstantBuffers( 0, 0, nullptr );
			context->CSSetShader( nullptr, nullptr, 0 );
		}
	};

	void WrappedIVRCompositor::Submit(EVREye eEye, const Texture_t *pTexture, const VRTextureBounds_t* pBounds, EVRSubmitFlags nSubmitFlags) {
		ID3D11Texture2D *texture = (ID3D11Texture2D*)pTexture->handle;

		if ( pTexture->eType == TextureType_DirectX && Config::Instance().fsrEnabled ) {
			if (fsrResources == nullptr) {
				fsrResources = new FSRRenderResources;
				fsrResources->Create( texture );
				log() << std::flush;
			}

			if (GetTickCount() - fsrResources->lastSwitch > 5000 && eyeCount == 0) {
				fsrResources->lastSwitch = GetTickCount();
				fsrResources->enabled = !fsrResources->enabled || !Config::Instance().alternate;
			}

			// if a single texture is used for both eyes, only apply effects on the first Submit
			if (fsrResources->created && fsrResources->enabled && (eyeCount == 0 || texture != lastSubmittedTexture)) {
				fsrResources->Apply(texture);
			}
			lastSubmittedTexture = texture;
			eyeCount = (eyeCount + 1) % 2;

			if (fsrResources->created && fsrResources->enabled) {
				const_cast<Texture_t*>(pTexture)->handle = fsrResources->outputTexture.Get();
			}
		}

		if (fsrResources && fsrResources->created) {
			if (GetTickCount() - fsrResources->lastSwitch > 5000 && eEye == Eye_Left) {
				fsrResources->lastSwitch = GetTickCount();
				fsrResources->enabled = !fsrResources->enabled;
			}
			if (fsrResources->enabled || !Config::Instance().alternate) {
				const_cast<Texture_t*>(pTexture)->handle = fsrResources->outputTexture.Get();
			}
		}
	}

	WrappedIVRCompositor::~WrappedIVRCompositor() {
		delete fsrResources;
	}
}
