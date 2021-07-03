#pragma once
#include <fstream>
#include "json/json.h"

std::ostream& Log();
std::string GetDllPath();

struct Config {
	bool enableOculusEmulationFix = false;
	bool fsrEnabled = false;
	float fsrQuality = 0.75f;
	float sharpness = 1.f;
	bool alternate = false;

	static Config Load() {
		Config config;
		try {
			std::ifstream configFile (GetDllPath() + "\\openvr_mod.cfg");
			if (configFile.is_open()) {
				Json::Value root;
				configFile >> root;
				config.enableOculusEmulationFix = root.get( "enable_oculus_emulation_fix", false ).asBool();
				Json::Value fsr = root.get("fsr", Json::Value());
				config.fsrEnabled = fsr.get("enabled", false).asBool();
				config.sharpness = fsr.get("sharpness", 1.0).asFloat();
				if (config.sharpness < 0) config.sharpness = 0;
				//if (config.sharpness > 1) config.sharpness = 1;
				config.fsrQuality = fsr.get("quality", 0.75).asFloat();
				config.alternate = fsr.get("alternate", false).asBool();
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
