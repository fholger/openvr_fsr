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
		bool textureContainsOnlyOneEye = true;
		bool requiresCopy = false;
		bool inputIsSrgb = false;
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11SamplerState> sampler;

		struct EyeViews {
			ComPtr<ID3D11ShaderResourceView> view[2];
		};
		std::unordered_map<ID3D11Texture2D*, EyeViews> inputTextureViews;
		// in case the incoming texture can't be bound as an SRV, we'll need to prepare a copy
		ComPtr<ID3D11Texture2D> copiedTexture;
		ComPtr<ID3D11ShaderResourceView> copiedTextureView;

		void PrepareCopyResources(DXGI_FORMAT format);
		ID3D11ShaderResourceView *GetInputView(ID3D11Texture2D *inputTexture, int eye);

		// upscale resources
		ComPtr<ID3D11ComputeShader> upscaleShader;
		ComPtr<ID3D11Buffer> upscaleConstantsBuffer[2];
		ComPtr<ID3D11Texture2D> upscaledTexture;
		ComPtr<ID3D11UnorderedAccessView> upscaledTextureUav;
		ComPtr<ID3D11ShaderResourceView> upscaledTextureView;
		// NIS specific lookup textures
		ComPtr<ID3D11Texture2D> scalerCoeffTexture;
		ComPtr<ID3D11Texture2D> usmCoeffTexture;
		ComPtr<ID3D11ShaderResourceView> scalerCoeffView;
		ComPtr<ID3D11ShaderResourceView> usmCoeffView;

		void PrepareUpscalingResources(DXGI_FORMAT format);
		void ApplyUpscaling(EVREye eEye, ID3D11ShaderResourceView *inputView);

		// sharpening resources
		ComPtr<ID3D11ComputeShader> sharpenShader;
		ComPtr<ID3D11Buffer> sharpenConstantsBuffer[2];
		ComPtr<ID3D11Texture2D> sharpenedTexture;
		ComPtr<ID3D11UnorderedAccessView> sharpenedTextureUav;

		void PrepareSharpeningResources(DXGI_FORMAT format);
		void ApplySharpening(EVREye eEye, ID3D11ShaderResourceView *inputView);

		ID3D11Texture2D *lastSubmittedTexture = nullptr;
		ID3D11Texture2D *outputTexture = nullptr;
		int eyeCount = 0;

		void PrepareResources(ID3D11Texture2D *inputTexture, EColorSpace colorSpace);
		void ApplyPostProcess(EVREye eEye, ID3D11Texture2D *inputTexture);

		struct ProfileQuery {
			ComPtr<ID3D11Query> queryDisjoint;
			ComPtr<ID3D11Query> queryStart;
			ComPtr<ID3D11Query> queryEnd;
		};

		static const int QUERY_COUNT = 6;
		ProfileQuery profileQueries[QUERY_COUNT];
		int currentQuery = 0;
		float summedGpuTime = 0.0f;
		int countedQueries = 0;
	};
}
