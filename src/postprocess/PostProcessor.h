#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <unordered_map>
#include "openvr.h"

namespace vr {
	using Microsoft::WRL::ComPtr;

	class PostProcessor {
	public:
		void Apply(EVREye eEye, const Texture_t *pTexture, const VRTextureBounds_t* pBounds, EVRSubmitFlags nSubmitFlags);
		void Reset();

	private:
		bool enabled = true;
		bool initialized = false;
		uint32_t inputWidth = 0;
		uint32_t inputHeight = 0;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		bool requiresCopy = false;
		bool inputIsSrgb = false;
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11SamplerState> sampler;

		std::unordered_map<ID3D11Texture2D*, ComPtr<ID3D11ShaderResourceView>> inputTextureViews;
		// in case the incoming texture can't be bound as an SRV, we'll need to prepare a copy
		ComPtr<ID3D11Texture2D> copiedTexture;
		ComPtr<ID3D11ShaderResourceView> copiedTextureView;

		void PrepareCopyResources(DXGI_FORMAT format);
		ID3D11ShaderResourceView *GetInputView(ID3D11Texture2D *inputTexture);

		// FSR upscale
		ComPtr<ID3D11ComputeShader> upscaleShader;
		ComPtr<ID3D11Buffer> upscaleConstantsBuffer;
		ComPtr<ID3D11Texture2D> upscaledTexture;
		ComPtr<ID3D11UnorderedAccessView> upscaledTextureUav;
		ComPtr<ID3D11ShaderResourceView> upscaledTextureView;

		void PrepareUpscalingResources();
		void ApplyUpscaling(ID3D11ShaderResourceView *inputView);

		// rCAS sharpening
		ComPtr<ID3D11ComputeShader> rCASShader;
		ComPtr<ID3D11Buffer> sharpenConstantsBuffer;
		ComPtr<ID3D11Texture2D> sharpenedTexture;
		ComPtr<ID3D11UnorderedAccessView> sharpenedTextureUav;

		void PrepareSharpeningResources();
		void ApplySharpening(ID3D11ShaderResourceView *inputView);

		ID3D11Texture2D *lastSubmittedTexture = nullptr;
		ID3D11Texture2D *outputTexture = nullptr;
		int eyeCount = 0;

		void PrepareResources(ID3D11Texture2D *inputTexture, EColorSpace colorSpace);
		void ApplyPostProcess(ID3D11Texture2D *inputTexture);
	};
}