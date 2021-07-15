#pragma once
#include <d3d11.h>

void InitHooks();
void ShutdownHooks();

void HookVRInterface(const char *version, void *instance);
void HookD3D11Context(ID3D11DeviceContext *context, ID3D11Device *device, float mipLodBias);
