#pragma once
#include <string>
#include <cstdint>

namespace pulsedb {
    struct Config {
        uint16_t api_port = 7700;
        std::string data_directory = "C:/ProgramData/PulseDB/data";
        int collection_interval_ms = 1000;

        int retention_raw_days = 7;
        int retention_1min_days = 30;
        int retention_1hr_days = 365;
    };

    // loads config from the fixed path, creating it with defaults if it doesn't exist, and falling back to defaults per field if any problems arise
    Config load_config();
    // writes cfg to the same fixed path load_config() reads from
    bool save_config(const Config& cfg);
}