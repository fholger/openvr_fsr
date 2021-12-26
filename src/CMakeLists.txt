# Project name.
project(openvr_api)

set( LIBNAME "openvr_api" )
set(OPENVR_HEADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../headers)

# Set some properies for specific files.
if(APPLE)
  set(CMAKE_MACOSX_RPATH 1)
  if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set_source_files_properties(vrcommon/pathtools_public.cpp vrcommon/vrpathregistry_public.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
  endif()
  if(BUILD_SHARED OR BUILD_FRAMEWORK)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    mark_as_advanced(FOUNDATION_FRAMEWORK)
    set(EXTRA_LIBS ${EXTRA_LIBS} ${FOUNDATION_FRAMEWORK})
  endif(BUILD_SHARED OR BUILD_FRAMEWORK)
elseif(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_definitions( -DWIN64 )
    #set( LIBNAME "openvr_api64" )
  endif()
endif()

# Add include folders.
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../headers ${CMAKE_CURRENT_SOURCE_DIR}/vrcommon ${CMAKE_CURRENT_SOURCE_DIR}/postprocess ${CMAKE_CURRENT_SOURCE_DIR}/minhook/include ${CMAKE_CURRENT_BINARY_DIR})

if(USE_CUSTOM_LIBCXX)
	link_directories(
		${LIBCXX_LIB_DIR}
	)
endif()

# Set the source group and files.
set(CORE_FILES
	openvr_api_public.cpp
	jsoncpp.cpp
)
set(VRCOMMON_FILES
	vrcommon/dirtools_public.cpp
	vrcommon/envvartools_public.cpp
	vrcommon/pathtools_public.cpp
	vrcommon/sharedlibtools_public.cpp
	vrcommon/hmderrors_public.cpp
	vrcommon/vrpathregistry_public.cpp
	vrcommon/strtools_public.cpp
)
set(POSTPROCESS_FILES
	postprocess/Config.h
	postprocess/Config.cpp
	postprocess/VrHooks.h
	postprocess/VrHooks.cpp
	postprocess/PostProcessor.h
	postprocess/PostProcessor.cpp
	postprocess/ScreenGrab11.h
	postprocess/ScreenGrab11.cpp
)
set(FSR_FILES
	fsr/ffx_a.h
	fsr/ffx_fsr1.h
	fsr/fsr_easu.hlsl
	fsr/fsr_rcas.hlsl
)
set(NIS_FILES
	nis/NIS_Config.h
	nis/NIS_Scaler.h
	nis/NIS_Upscale.hlsl
	nis/NIS_Sharpen.hlsl
)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(MINHOOK_HDE minhook/src/hde/hde64.c)
else()
	set(MINHOOK_HDE minhook/src/hde/hde32.c)
endif()
set(MINHOOK_FILES
	minhook/src/buffer.c
	minhook/src/buffer.h
	minhook/src/hook.c
	minhook/src/trampoline.c
	"${MINHOOK_HDE}"
)

set(SOURCE_FILES
	${CORE_FILES}
	${VRCOMMON_FILES}
	${POSTPROCESS_FILES}
	${FSR_FILES}
	${NIS_FILES}
	${MINHOOK_FILES}
)

set(PUBLIC_HEADER_FILES
	${OPENVR_HEADER_DIR}/openvr_driver.h
	${OPENVR_HEADER_DIR}/openvr_capi.h
	${OPENVR_HEADER_DIR}/openvr.h
)

source_group("Src" FILES
	${CORE_FILES}
)

source_group("VRCommon" FILES
	${VRCOMMON_FILES}
)

source_group("PostProcess" FILES
	${POSTPROCESS_FILES}
)

source_group("FSR" FILES
	${FSR_FILES}
)

source_group("NIS" FILES
	${NIS_FILES}
)

source_group("MinHook" FILES
	${MINHOOK_FILES}
)

# Build the library.
if(BUILD_SHARED)
	add_library(${LIBNAME} SHARED ${SOURCE_FILES})
elseif(BUILD_FRAMEWORK)
	set( LIBNAME "OpenVR" )
	add_library( ${LIBNAME} 
			SHARED ${SOURCE_FILES}
			${CMAKE_SOURCE_DIR}/headers/openvr.h
			${CMAKE_SOURCE_DIR}/headers/openvr_api.cs
			${CMAKE_SOURCE_DIR}/headers/openvr_api.json
			${CMAKE_SOURCE_DIR}/headers/openvr_capi.h
			${CMAKE_SOURCE_DIR}/headers/openvr_driver.h
	)
	set_target_properties(OpenVR PROPERTIES
		FRAMEWORK TRUE
		FRAMEWORK_VERSION A
		MACOSX_FRAMEWORK_IDENTIFIER com.valvesoftware.OpenVR.framework
		MACOSX_FRAMEWORK_INFO_PLIST ${CMAKE_SOURCE_DIR}/src/Info.plist
		# "current version" in semantic format in Mach-O binary file
		VERSION 1.0.6
		# "compatibility version" in semantic format in Mach-O binary file
		SOVERSION 1.0.0
		PUBLIC_HEADER "${CMAKE_SOURCE_DIR}/headers/openvr.h;${CMAKE_SOURCE_DIR}/headers/openvr_api.cs;${CMAKE_SOURCE_DIR}/headers/openvr_api.json;${CMAKE_SOURCE_DIR}/headers/openvr_capi.h;${CMAKE_SOURCE_DIR}/headers/openvr_driver.h"
		LINKER_LANGUAGE CXX
	)
else()
	add_library(${LIBNAME} STATIC ${SOURCE_FILES})
endif()

if(USE_CUSTOM_LIBCXX)
	set(EXTRA_LIBS ${EXTRA_LIBS} c++ c++abi)
endif()

set_property(SOURCE fsr/fsr_easu.hlsl PROPERTY VS_SHADER_TYPE Compute)
set_property(SOURCE fsr/fsr_easu.hlsl PROPERTY VS_SHADER_MODEL "5.0")
set_property(SOURCE fsr/fsr_easu.hlsl PROPERTY VS_SHADER_OUTPUT_HEADER_FILE "shader_fsr_easu.h")
set_property(SOURCE fsr/fsr_easu.hlsl PROPERTY VS_SHADER_VARIABLE_NAME "g_FSRUpscaleShader")
set_property(SOURCE fsr/fsr_rcas.hlsl PROPERTY VS_SHADER_TYPE Compute)
set_property(SOURCE fsr/fsr_rcas.hlsl PROPERTY VS_SHADER_MODEL "5.0")
set_property(SOURCE fsr/fsr_rcas.hlsl PROPERTY VS_SHADER_OUTPUT_HEADER_FILE "shader_fsr_rcas.h")
set_property(SOURCE fsr/fsr_rcas.hlsl PROPERTY VS_SHADER_VARIABLE_NAME "g_FSRSharpenShader")
set_property(SOURCE nis/NIS_Upscale.hlsl PROPERTY VS_SHADER_TYPE Compute)
set_property(SOURCE nis/NIS_Upscale.hlsl PROPERTY VS_SHADER_MODEL "5.0")
set_property(SOURCE nis/NIS_Upscale.hlsl PROPERTY VS_SHADER_OUTPUT_HEADER_FILE "shader_nis_upscale.h")
set_property(SOURCE nis/NIS_Upscale.hlsl PROPERTY VS_SHADER_VARIABLE_NAME "g_NISUpscaleShader")
set_property(SOURCE nis/NIS_Sharpen.hlsl PROPERTY VS_SHADER_TYPE Compute)
set_property(SOURCE nis/NIS_Sharpen.hlsl PROPERTY VS_SHADER_MODEL "5.0")
set_property(SOURCE nis/NIS_Sharpen.hlsl PROPERTY VS_SHADER_OUTPUT_HEADER_FILE "shader_nis_sharpen.h")
set_property(SOURCE nis/NIS_Sharpen.hlsl PROPERTY VS_SHADER_VARIABLE_NAME "g_NISSharpenShader")

set(EXTRA_LIBS ${EXTRA_LIBS} dxguid)
target_link_libraries(${LIBNAME} ${EXTRA_LIBS} ${CMAKE_DL_LIBS})
target_include_directories(${LIBNAME} PUBLIC ${OPENVR_HEADER_DIR})

add_custom_command(TARGET ${LIBNAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy
		${CMAKE_CURRENT_SOURCE_DIR}/openvr_mod.cfg
		$<TARGET_FILE_DIR:${LIBNAME}>
)

install(TARGETS ${LIBNAME} DESTINATION lib)
install(FILES ${PUBLIC_HEADER_FILES} DESTINATION include/openvr)

# Generate a .pc file for linux environments
if(PLATFORM_NAME MATCHES "linux")
    set(INSTALL_PKGCONFIG_DIR "${CMAKE_INSTALL_PREFIX}/share/pkgconfig" CACHE PATH "Installation directory for pkgconfig (.pc) files")
    CONFIGURE_FILE("openvr.pc.in" "openvr.pc" @ONLY)

    set(OPENVR_PC ${CMAKE_CURRENT_BINARY_DIR}/openvr.pc)
    if(NOT SKIP_INSTALL_FILES AND NOT SKIP_INSTALL_ALL )
        install(FILES ${OPENVR_PC} DESTINATION "${INSTALL_PKGCONFIG_DIR}")
    endif()
endif()
