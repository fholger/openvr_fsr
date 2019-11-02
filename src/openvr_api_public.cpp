//========= Copyright Valve Corporation ============//
#define VR_API_EXPORT 1
#include "openvr.h"
#include "ivrclientcore.h"
#include <vrcore/pathtools_public.h>
#include <vrcore/sharedlibtools_public.h>
#include <vrcore/envvartools_public.h>
#include "hmderrors_public.h"
#include <vrcore/strtools_public.h>
#include <vrcore/vrpathregistry_public.h>
#include "WrappedIVRCompositor.h"
#include <mutex>

#include "Config.h"
#include "MinHook.h"
#undef interface

using vr::EVRInitError;
using vr::IVRSystem;
using vr::IVRClientCore;
using vr::VRInitError_None;

MH_STATUS WINAPI MH_CreateHookVirtualEx(
    LPVOID pInstance, UINT methodPos, LPVOID pDetour, LPVOID *ppOriginal, LPVOID *ppTarget)
{
    LPVOID* pVMT = *((LPVOID**)pInstance);
    LPVOID  pTarget = pVMT[methodPos];

    if (ppTarget != NULL)
        *ppTarget = pTarget;
    return MH_CreateHook(pTarget, pDetour, ppOriginal);
}

// figure out how to import from the VR API dll
#if defined(_WIN32)

#if !defined(OPENVR_BUILD_STATIC)
#define VR_EXPORT_INTERFACE extern "C" __declspec( dllexport )
#else
#define VR_EXPORT_INTERFACE extern "C"
#endif

#elif defined(__GNUC__) || defined(COMPILER_GCC) || defined(__APPLE__)

#define VR_EXPORT_INTERFACE extern "C" __attribute__((visibility("default")))


#else
#error "Unsupported Platform."
#endif

namespace vr
{
namespace {
	WrappedIVRCompositor wrappedCompositor;
}

static void *g_pVRModule = NULL;
static IVRClientCore *g_pHmdSystem = NULL;
static std::recursive_mutex g_mutexSystem;


typedef void* (*VRClientCoreFactoryFn)(const char *pInterfaceName, int *pReturnCode);

static uint32_t g_nVRToken = 0;

uint32_t VR_GetInitToken()
{
	return g_nVRToken;
}

EVRInitError VR_LoadHmdSystemInternal();
void CleanupInternalInterfaces();


uint32_t VR_InitInternal2( EVRInitError *peError, vr::EVRApplicationType eApplicationType, const char *pStartupInfo )
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

	EVRInitError err = VR_LoadHmdSystemInternal();
	if ( err == vr::VRInitError_None )
	{
		err = g_pHmdSystem->Init( eApplicationType, pStartupInfo );
	}

	if ( peError )
		*peError = err;

	if ( err != VRInitError_None )
	{
		SharedLib_Unload( g_pVRModule );
		g_pHmdSystem = NULL;
		g_pVRModule = NULL;

		return 0;
	}

	return ++g_nVRToken;
}

VR_INTERFACE uint32_t VR_CALLTYPE VR_InitInternal( EVRInitError *peError, EVRApplicationType eApplicationType );

uint32_t VR_InitInternal( EVRInitError *peError, vr::EVRApplicationType eApplicationType )
{
	return VR_InitInternal2( peError, eApplicationType, nullptr );
}

void VR_ShutdownInternal()
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

#if !defined( VR_API_PUBLIC )
	CleanupInternalInterfaces();
#endif

	if ( g_pHmdSystem )
	{
		g_pHmdSystem->Cleanup();
		g_pHmdSystem = NULL;
	}

	if ( g_pVRModule )
	{
		SharedLib_Unload( g_pVRModule );
		g_pVRModule = NULL;
	}

	++g_nVRToken;
}

EVRInitError VR_LoadHmdSystemInternal()
{
	std::string sRuntimePath, sConfigPath, sLogPath;

	bool bReadPathRegistry = CVRPathRegistry_Public::GetPaths( &sRuntimePath, &sConfigPath, &sLogPath, NULL, NULL );
	if( !bReadPathRegistry )
	{
		return vr::VRInitError_Init_PathRegistryNotFound;
	}

	// figure out where we're going to look for vrclient.dll
	// see if the specified path actually exists.
	if( !Path_IsDirectory( sRuntimePath ) )
	{
		return vr::VRInitError_Init_InstallationNotFound;
	}

	// Because we don't have a way to select debug vs. release yet we'll just
	// use debug if it's there
#if defined( LINUX64 ) || defined( LINUXARM64 )
	std::string sTestPath = Path_Join( sRuntimePath, "bin", PLATSUBDIR );
#else
	std::string sTestPath = Path_Join( sRuntimePath, "bin" );
#endif
	if( !Path_IsDirectory( sTestPath ) )
	{
		return vr::VRInitError_Init_InstallationCorrupt;
	}

#if defined( WIN64 )
	std::string sDLLPath = Path_Join( sTestPath, "vrclient_x64" DYNAMIC_LIB_EXT );
#else
	std::string sDLLPath = Path_Join( sTestPath, "vrclient" DYNAMIC_LIB_EXT );
#endif

	// only look in the override
	void *pMod = SharedLib_Load( sDLLPath.c_str() );
	// nothing more to do if we can't load the DLL
	if( !pMod )
	{
		return vr::VRInitError_Init_VRClientDLLNotFound;
	}

	VRClientCoreFactoryFn fnFactory = ( VRClientCoreFactoryFn )( SharedLib_GetFunction( pMod, "VRClientCoreFactory" ) );
	if( !fnFactory )
	{
		SharedLib_Unload( pMod );
		return vr::VRInitError_Init_FactoryNotFound;
	}

	int nReturnCode = 0;
	g_pHmdSystem = static_cast< IVRClientCore * > ( fnFactory( vr::IVRClientCore_Version, &nReturnCode ) );
	if( !g_pHmdSystem )
	{
		SharedLib_Unload( pMod );
		return vr::VRInitError_Init_InterfaceNotFound;
	}

	g_pVRModule = pMod;
	return VRInitError_None;
}


typedef void (*IVRSystem_GetRecommendedRenderTargetSize_Orig)(IVRSystem *self, uint32_t *pnWidth, uint32_t *pnHeight);
IVRSystem_GetRecommendedRenderTargetSize_Orig pOrigGetRecommendedRenderTargetSize = nullptr;
	
static void IVRSystem_GetRecommendedRenderTargetSize(IVRSystem *self, uint32_t *pnWidth, uint32_t *pnHeight) {
	pOrigGetRecommendedRenderTargetSize(self, pnWidth, pnHeight);
	if (Config::Instance().fsrEnabled && Config::Instance().fsrQuality < 1) {
		*pnWidth *= Config::Instance().fsrQuality;
		*pnHeight *= Config::Instance().fsrQuality;
	}
	log() << "Recommended render target size: " << *pnWidth << "x" << *pnHeight << "\n";
}

typedef EVRCompositorError (*IVRCompositor_Submit_012_Orig)(IVRCompositor *self, EVREye eEye, const Texture_t *pTexture, const VRTextureBounds_t *pBounds, EVRSubmitFlags nSubmitFlags);
IVRCompositor_Submit_012_Orig pOrigSubmit012 = nullptr;

static EVRCompositorError IVRCompositor_Submit_012(IVRCompositor *self, EVREye eEye, const Texture_t *pTexture, const VRTextureBounds_t *pBounds, EVRSubmitFlags nSubmitFlags) {
	wrappedCompositor.Submit(eEye, pTexture, pBounds, nSubmitFlags);
	return pOrigSubmit012(self, eEye, pTexture, pBounds, nSubmitFlags);
}


void *VR_GetGenericInterface(const char *pchInterfaceVersion, EVRInitError *peError)
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );
	MH_Initialize();

	if (!g_pHmdSystem)
	{
		if (peError)
			*peError = vr::VRInitError_Init_NotInitialized;
		return NULL;
	}

	void *interface = g_pHmdSystem->GetGenericInterface(pchInterfaceVersion, peError);
	// Only install hooks once, for the first interface version encountered to avoid duplicated hooks
	// This is necessary because vrclient.dll may create an internal instance with a different version than the application to translate older versions, which with hooks installed for both would cause an infinite loop

	static unsigned int system_version = 0;
	if (system_version == 0 && std::sscanf(pchInterfaceVersion, "IVRSystem_%u", &system_version)) {
		LPVOID pTarget = nullptr;
		// The 'IVRSystem::GetRecommendedRenderTargetSize' function definition has been the same since the initial
		// release of OpenVR; however, in early versions there was an additional method in front of it.
		UINT methodPos = (system_version >= 9 ? 0 : 1);
		MH_CreateHookVirtualEx(interface, methodPos, IVRSystem_GetRecommendedRenderTargetSize, (void**)&pOrigGetRecommendedRenderTargetSize, &pTarget);
		if (pTarget) {
			log() << "Injecting " << pchInterfaceVersion << std::endl;
			MH_EnableHook(pTarget);
		}
	}

	static unsigned int compositor_version = 0;
	if (compositor_version == 0 && std::sscanf(pchInterfaceVersion, "IVRCompositor_%u", &compositor_version))
	{
		// The 'IVRCompositor::Submit' function definition has been stable and has had the same virtual function table index since the OpenVR 1.0 release (which was at 'IVRCompositor_015')
		LPVOID pTarget = nullptr;
		if (compositor_version >= 12)
			MH_CreateHookVirtualEx(interface, 5, IVRCompositor_Submit_012, (void**)&pOrigSubmit012, &pTarget);
		/*else if (compositor_version >= 9)
			reshade::hooks::install("IVRCompositor::Submit", vtable_from_instance(static_cast<vr::IVRCompositor *>(interface_instance)), 4, reinterpret_cast<reshade::hook::address>(IVRCompositor_Submit_009));
		else if (compositor_version == 8)
			reshade::hooks::install("IVRCompositor::Submit", vtable_from_instance(static_cast<vr::IVRCompositor *>(interface_instance)), 6, reinterpret_cast<reshade::hook::address>(IVRCompositor_Submit_008));
		else if (compositor_version == 7)
			reshade::hooks::install("IVRCompositor::Submit", vtable_from_instance(static_cast<vr::IVRCompositor *>(interface_instance)), 6, reinterpret_cast<reshade::hook::address>(IVRCompositor_Submit_007));
		*/
		if (pTarget) {
			log() << "Injecting " << pchInterfaceVersion << std::endl;
			MH_EnableHook(pTarget);
		}
	}

	return interface;
}

bool VR_IsInterfaceVersionValid(const char *pchInterfaceVersion)
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

	if (!g_pHmdSystem)
	{
		return false;
	}

	return g_pHmdSystem->IsInterfaceVersionValid(pchInterfaceVersion) == VRInitError_None;
}

bool VR_IsHmdPresent()
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

	if( g_pHmdSystem )
	{
		// if we're already initialized, just call through
		return g_pHmdSystem->BIsHmdPresent();
	}
	else
	{
		// otherwise we need to do a bit more work
		EVRInitError err = VR_LoadHmdSystemInternal();
		if( err != VRInitError_None )
			return false;

		bool bHasHmd = g_pHmdSystem->BIsHmdPresent();

		g_pHmdSystem = NULL;
		SharedLib_Unload( g_pVRModule );
		g_pVRModule = NULL;

		return bHasHmd;
	}
}

/** Returns true if the OpenVR runtime is installed. */
bool VR_IsRuntimeInstalled()
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

	if( g_pHmdSystem )
	{
		// if we're already initialized, OpenVR is obviously installed
		return true;
	}
	else
	{
		// otherwise we need to do a bit more work
		std::string sRuntimePath, sConfigPath, sLogPath;

		bool bReadPathRegistry = CVRPathRegistry_Public::GetPaths( &sRuntimePath, &sConfigPath, &sLogPath, NULL, NULL );
		if( !bReadPathRegistry )
		{
			return false;
		}

		// figure out where we're going to look for vrclient.dll
		// see if the specified path actually exists.
		if( !Path_IsDirectory( sRuntimePath ) )
		{
			return false;
		}

		// the installation may be corrupt in some way, but it certainly looks installed
		return true;
	}
}


// -------------------------------------------------------------------------------
// Purpose: This is the old Runtime Path interface that is no longer exported in the
//			latest header. We still want to export it from the DLL, though, so updating
//			to a new DLL doesn't break old compiled code. This version was not thread 
//			safe and could change the buffer pointer to by a previous result on a 
//			subsequent call
// -------------------------------------------------------------------------------
VR_EXPORT_INTERFACE const char *VR_CALLTYPE VR_RuntimePath();

/** Returns where OpenVR runtime is installed. */
const char *VR_RuntimePath()
{
	static char rchBuffer[1024];
	uint32_t unRequiredSize;
	if ( VR_GetRuntimePath( rchBuffer, sizeof( rchBuffer ), &unRequiredSize ) && unRequiredSize < sizeof( rchBuffer ) )
	{
		return rchBuffer;
	}
	else
	{
		return nullptr;
	}
}


/** Returns where OpenVR runtime is installed. */
bool VR_GetRuntimePath( char *pchPathBuffer, uint32_t unBufferSize, uint32_t *punRequiredBufferSize )
{
	// otherwise we need to do a bit more work
	std::string sRuntimePath;

	*punRequiredBufferSize = 0;

	bool bReadPathRegistry = CVRPathRegistry_Public::GetPaths( &sRuntimePath, nullptr, nullptr, nullptr, nullptr );
	if ( !bReadPathRegistry )
	{
		return false;
	}

	// figure out where we're going to look for vrclient.dll
	// see if the specified path actually exists.
	if ( !Path_IsDirectory( sRuntimePath ) )
	{
		return false;
	}

	*punRequiredBufferSize = (uint32_t)sRuntimePath.size() + 1;
	if ( sRuntimePath.size() >= unBufferSize )
	{
		*pchPathBuffer = '\0';
	}
	else
	{
		strcpy_safe( pchPathBuffer, unBufferSize, sRuntimePath.c_str() );
	}

	return true;
}


/** Returns the symbol version of an HMD error. */
const char *VR_GetVRInitErrorAsSymbol( EVRInitError error )
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

	if( g_pHmdSystem )
		return g_pHmdSystem->GetIDForVRInitError( error );
	else
		return GetIDForVRInitError( error );
}


/** Returns the english string version of an HMD error. */
const char *VR_GetVRInitErrorAsEnglishDescription( EVRInitError error )
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

	if ( g_pHmdSystem )
		return g_pHmdSystem->GetEnglishStringForHmdError( error );
	else
		return GetEnglishStringForHmdError( error );
}


VR_INTERFACE const char *VR_CALLTYPE VR_GetStringForHmdError( vr::EVRInitError error );

/** Returns the english string version of an HMD error. */
const char *VR_GetStringForHmdError( EVRInitError error )
{
	return VR_GetVRInitErrorAsEnglishDescription( error );
}

}

