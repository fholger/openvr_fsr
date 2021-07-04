#pragma once
#include <fstream>
#include "json/json.h"

std::ostream& Log();
std::string GetDllPath();

struct Config {
	bool fsrEnabled = false;
	float renderScale = 1.f;
	float sharpness = 0.75f;

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
