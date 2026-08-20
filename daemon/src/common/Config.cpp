#include <fstream>
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "Config.h"

namespace pulsedb {
	static const std::string CONFIG_PATH = "C:/ProgramData/PulseDB/pulsedb.json";

    // turns a Config struct into json, used both for writing defaults and for reference
    static nlohmann::json config_to_json(const Config& cfg) {
        nlohmann::json j;
        j["api_port"] = cfg.api_port;
        j["data_directory"] = cfg.data_directory;
        j["collection_interval_ms"] = cfg.collection_interval_ms;
        j["retention"]["raw_days"] = cfg.retention_raw_days;
        j["retention"]["summary_1min_days"] = cfg.retention_1min_days;
        j["retention"]["summary_1hr_days"] = cfg.retention_1hr_days;
        return j;
    }

    
    // pulls one field out of json into cfg, falls back to whatever default is already
    // sitting in cfg if the key is missing or the wrong type, and warns either way
    template<typename T>
    static void read_field(const nlohmann::json& j, const std::string& key, T& out) {
        if (!j.contains(key)) {
            std::cerr << "config: missing '" << key << "', using default\n";
            return;
        }
        try {
            out = j.at(key).get<T>();
        }
        catch (...) {
            std::cerr << "config: bad value for '" << key << "', using default\n";
        }
    }

    Config load_config() {
        Config cfg; // defaults live here already, from the struct's member initializers

        std::filesystem::path path(CONFIG_PATH);

        if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path);
            out << config_to_json(cfg).dump(4);
            std::cout << "config: no pulsedb.json found, wrote defaults to " << CONFIG_PATH << "\n";
            return cfg;
        }

        std::ifstream in(path);
        nlohmann::json j;
        try {
            in >> j;
        }
        catch (...) {
            std::cerr << "config: pulsedb.json is not valid json, using all defaults\n";
            return cfg;
        }

        read_field(j, "api_port", cfg.api_port);
        read_field(j, "data_directory", cfg.data_directory);
        read_field(j, "collection_interval_ms", cfg.collection_interval_ms);

        if (j.contains("retention")) {
            auto& r = j["retention"];
            read_field(r, "raw_days", cfg.retention_raw_days);
            read_field(r, "summary_1min_days", cfg.retention_1min_days);
            read_field(r, "summary_1hr_days", cfg.retention_1hr_days);
        }
        else {
            std::cerr << "config: missing 'retention' block, using defaults\n";
        }

        return cfg;
    }
}