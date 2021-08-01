#pragma once
#include <fstream>
#include "json/json.h"

std::ostream& Log();
std::string GetDllPath();

struct Config {
	bool fsrEnabled = false;
	bool center_display = true;
	bool applyMIPBias = true;
	float renderScale = 1.f;
	float sharpness = 0.75f;
	float radius = 0.5f;
	float rcas_radius = 0.5f;

	static Config Load() {
		Config config;
		try {
			std::ifstream configFile (GetDllPath() + "\\openvr_mod.cfg");
			if (configFile.is_open()) {
				Json::Value root;
				configFile >> root;
				Json::Value fsr = root.get("fsr", Json::Value());
				config.fsrEnabled = fsr.get("enabled", false).asBool();
				config.center_display = fsr.get("center_display", false).asBool();
				config.sharpness = fsr.get("sharpness", 1.0).asFloat();
				if (config.sharpness < 0) config.sharpness = 0;
				config.renderScale = fsr.get("renderScale", 1.0).asFloat();
				config.applyMIPBias = fsr.get("applyMIPBias", true).asBool();
				config.radius = fsr.get("radius", 0.5).asFloat();
				config.rcas_radius = fsr.get("rcas_radius", 0.5).asFloat();
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
