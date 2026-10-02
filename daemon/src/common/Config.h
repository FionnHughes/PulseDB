#pragma once
#include <cstdint>
#include <string>

namespace pulsedb {
    std::string default_config_path();
    std::string default_data_dir();

    struct Config {
        uint16_t api_port = 7700;
        std::string data_directory = default_data_dir();
        int collection_interval_ms = 1000;

        int retention_raw_days = 7;
        int retention_1min_days = 30;
        int retention_1hr_days = 365;
    };

    // loads config from the fixed path, creating it with defaults if it doesn't exist, and falling back to defaults per field if any problems arise
    Config load_config();
    // writes cfg to the same fixed path load_config() reads from
    bool save_config(const Config& cfg);
} // namespace pulsedb
