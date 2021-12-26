#pragma once
#include <fstream>

#include "PostProcessor.h"
#include "json/json.h"

std::ostream& Log();
std::string GetDllPath();

struct Config {
	bool fsrEnabled = false;
	bool applyMIPBias = true;
	float renderScale = 1.f;
	float sharpness = 0.75f;
	float radius = 0.5f;
	bool debugMode = false;
	bool useNis = false;
	bool hotkeysEnabled = true;
	bool hotkeysRequireCtrl = false;
	bool hotkeysRequireAlt = false;
	bool hotkeysRequireShift = false;
	int hotkeyToggleUseNis = VK_F1;
	int hotkeyToggleDebugMode = VK_F2;
	int hotkeyDecreaseSharpness = VK_F3;
	int hotkeyIncreaseSharpness = VK_F4;
	int hotkeyDecreaseRadius = VK_F5;
	int hotkeyIncreaseRadius = VK_F6;

	static Config Load() {
		Config config;
		try {
			std::ifstream configFile (GetDllPath() + "\\openvr_mod.cfg");
			if (configFile.is_open()) {
				Json::Value root;
				configFile >> root;
				Json::Value fsr = root.get("fsr", Json::Value());
				config.fsrEnabled = fsr.get("enabled", false).asBool();
				config.sharpness = fsr.get("sharpness", 1.0).asFloat();
				if (config.sharpness < 0) config.sharpness = 0;
				config.renderScale = fsr.get("renderScale", 1.0).asFloat();
				config.applyMIPBias = fsr.get("applyMIPBias", true).asBool();
				config.radius = fsr.get("radius", 0.5).asFloat();
				config.debugMode = fsr.get("debugMode", false).asBool();
				config.useNis = fsr.get("useNIS", false).asBool();
				Json::Value hotkeys = fsr.get("hotkeys", Json::Value());
				config.hotkeysEnabled = hotkeys.get("enabled", true).asBool();
				config.hotkeysRequireCtrl = hotkeys.get("requireCtrl", false).asBool();
				config.hotkeysRequireAlt = hotkeys.get("requireAlt", false).asBool();
				config.hotkeysRequireShift = hotkeys.get("requireShift", false).asBool();
				config.hotkeyToggleUseNis = hotkeys.get("toggleUseNIS", VK_F1).asInt();
				config.hotkeyToggleDebugMode = hotkeys.get("toggleDebugMode", VK_F2).asInt();
				config.hotkeyDecreaseSharpness = hotkeys.get("decreaseSharpness", VK_F3).asInt();
				config.hotkeyIncreaseSharpness = hotkeys.get("increaseSharpness", VK_F4).asInt();
				config.hotkeyDecreaseRadius = hotkeys.get("decreaseRadius", VK_F5).asInt();
				config.hotkeyIncreaseRadius = hotkeys.get("increaseRadius", VK_F6).asInt();
			}
		} catch (...) {
			Log() << "Could not read config file.\n";
		}
		return config;
	}

	static Config& Instance() {
		static Config instance = Load();
		return instance;
	}
};
