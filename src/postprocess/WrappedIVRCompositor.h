#pragma once
#include <d3d11.h>
#include "openvr.h"

namespace vr {
	class WrappedIVRCompositor {
	public:
		void Submit(EVREye eEye, const Texture_t *pTexture, const VRTextureBounds_t* pBounds, EVRSubmitFlags nSubmitFlags);
		~WrappedIVRCompositor();

	private:
		struct FSRRenderResources;
		FSRRenderResources *fsrResources = nullptr;
		ID3D11Texture2D *lastSubmittedTexture = nullptr;
		int eyeCount = 0;
	};
}
