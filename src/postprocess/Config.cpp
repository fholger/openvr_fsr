#include "Config.h"
#include <iostream>
#include <windows.h>

namespace {
	void helper() {}
}

std::wstring GetDllPath() {
	wchar_t path[FILENAME_MAX];
	HMODULE hm = nullptr;

	void *address = helper;		

	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address, &hm)) {
		return L".";
	}

	GetModuleFileNameW(hm, path, sizeof(path));

	std::wstring p = path;
	return p.substr(0, p.find_last_of('\\'));
}

std::ostream& Log() {
	try {
		static std::ofstream logFile (GetDllPath() + L"\\openvr_mod.log");
		return logFile;
	} catch (...) {
		return std::cout;
	}
}
