#pragma once

void InitHooks();
void ShutdownHooks();

void HookVRInterface(const char *version, void *instance);
