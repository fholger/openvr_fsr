#include "VrHooks.h"
#include "Config.h"
#include "PostProcessor.h"

#include <openvr.h>
#include <MinHook.h>
#include <unordered_map>


namespace {
	std::unordered_map<void*, void*> hooksToOriginal;
	bool ivrSystemHooked = false;
	bool ivrCompositorHooked = false;

	vr::PostProcessor postProcessor;

	void InstallVirtualFunctionHook(void *instance, uint32_t methodPos, void *hookFunction) {
		LPVOID* vtable = *((LPVOID**)instance);
		LPVOID  pTarget = vtable[methodPos];

		LPVOID pOriginal = nullptr;
		MH_CreateHook(pTarget, hookFunction, &pOriginal);
		MH_EnableHook(pTarget);

		hooksToOriginal[hookFunction] = pOriginal;
	}

	template<typename T>
	T CallOriginal(T hookFunction) {
		return (T)hooksToOriginal[hookFunction];
	}

	void IVRSystem_GetRecommendedRenderTargetSize(vr::IVRSystem *self, uint32_t *pnWidth, uint32_t *pnHeight) {
		CallOriginal(IVRSystem_GetRecommendedRenderTargetSize)(self, pnWidth, pnHeight);

		if (Config::Instance().fsrEnabled && Config::Instance().renderScale < 1) {
			*pnWidth *= Config::Instance().renderScale;
			*pnHeight *= Config::Instance().renderScale;
		}
		//Log() << "Recommended render target size: " << *pnWidth << "x" << *pnHeight << "\n";
	}

	vr::EVRCompositorError IVRCompositor_Submit(vr::IVRCompositor *self, vr::EVREye eEye, const vr::Texture_t *pTexture, const vr::VRTextureBounds_t *pBounds, vr::EVRSubmitFlags nSubmitFlags) {
		void *origHandle = pTexture->handle;

		postProcessor.Apply(eEye, pTexture, pBounds, nSubmitFlags);
		vr::EVRCompositorError error = CallOriginal(IVRCompositor_Submit)(self, eEye, pTexture, pBounds, nSubmitFlags);
		if (error != vr::VRCompositorError_None) {
			Log() << "Error when submitting for eye " << eEye << ": " << error << std::endl;
		}

		const_cast<vr::Texture_t*>(pTexture)->handle = origHandle;
		return error;
	}

	vr::EVRCompositorError IVRCompositor_Submit_008(vr::IVRCompositor *self, vr::EVREye eEye, unsigned int eTextureType, void *pTexture, const vr::VRTextureBounds_t *pBounds, vr::EVRSubmitFlags nSubmitFlags) {
		if (eTextureType == 0) {
			// texture type is DirectX
			vr::Texture_t texture;
			texture.eType = vr::TextureType_DirectX;
			texture.eColorSpace = vr::ColorSpace_Auto;
			texture.handle = pTexture;
			postProcessor.Apply(eEye, &texture, pBounds, nSubmitFlags);
			pTexture = texture.handle;
		}
		return CallOriginal(IVRCompositor_Submit_008)(self, eEye, eTextureType, pTexture, pBounds, nSubmitFlags);
	}

	vr::EVRCompositorError IVRCompositor_Submit_007(vr::IVRCompositor *self, vr::EVREye eEye, unsigned int eTextureType, void *pTexture, const vr::VRTextureBounds_t *pBounds) {
		if (eTextureType == 0) {
			// texture type is DirectX
			vr::Texture_t texture;
			texture.eType = vr::TextureType_DirectX;
			texture.eColorSpace = vr::ColorSpace_Auto;
			texture.handle = pTexture;
			postProcessor.Apply(eEye, &texture, pBounds, vr::Submit_Default);
			pTexture = texture.handle;
		}
		return CallOriginal(IVRCompositor_Submit_007)(self, eEye, eTextureType, pTexture, pBounds);
	}
}

void InitHooks() {
	Log() << "Initializing hooks...\n";
	MH_Initialize();
}

void ShutdownHooks() {
	Log() << "Shutting down hooks...\n";
	MH_Uninitialize();
	hooksToOriginal.clear();
	ivrSystemHooked = false;
	ivrCompositorHooked = false;
	postProcessor.Reset();
}

void HookVRInterface(const char *version, void *instance) {
	// Only install hooks once, for the first interface version encountered to avoid duplicated hooks
	// This is necessary because vrclient.dll may create an internal instance with a different version
	// than the application to translate older versions, which with hooks installed for both would cause
	// an infinite loop

	Log() << "Requested interface " << version << "\n";

	// -----------------------
	// - Hooks for IVRSystem -
	// -----------------------
	unsigned int system_version = 0;
	if (!ivrSystemHooked && std::sscanf(version, "IVRSystem_%u", &system_version)) {
		// The 'IVRSystem::GetRecommendedRenderTargetSize' function definition has been the same since the initial
		// release of OpenVR; however, in early versions there was an additional method in front of it.
		uint32_t methodPos = (system_version >= 9 ? 0 : 1);
		Log() << "Injecting GetRecommendedRenderTargetSize into " << version << std::endl;
		InstallVirtualFunctionHook(instance, methodPos, IVRSystem_GetRecommendedRenderTargetSize);

		ivrSystemHooked = true;
	}

	// ---------------------------
	// - Hooks for IVRCompositor -
	// ---------------------------
	unsigned int compositor_version = 0;
	if (!ivrCompositorHooked && std::sscanf(version, "IVRCompositor_%u", &compositor_version))
	{
		if (compositor_version >= 9) {
		Log() << "Injecting Submit into " << version << std::endl;
			uint32_t methodPos = compositor_version >= 12 ? 5 : 4;
			InstallVirtualFunctionHook(instance, methodPos, IVRCompositor_Submit);
			ivrCompositorHooked = true;
		}
		else if (compositor_version == 8) {
			Log() << "Injecting Submit into " << version << std::endl;
			InstallVirtualFunctionHook(instance, 6, IVRCompositor_Submit_008);
			ivrCompositorHooked = true;
		}
		else if (compositor_version == 7) {
			Log() << "Injecting Submit into " << version << std::endl;
			InstallVirtualFunctionHook(instance, 6, IVRCompositor_Submit_007);
			ivrCompositorHooked = true;
		}
	}
}
