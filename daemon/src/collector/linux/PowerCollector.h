#pragma once

#include <cstdint>
#include <string>

#include "../IMetricCollector.h"
#include "../MetricSnapshot.h"

namespace pulsedb {
    // reads power info from /sys/class/power_supply
    class PowerCollector : public IMetricCollector {
    public:
        explicit PowerCollector(std::string base_path = "/sys/class/power_supply", int refresh_interval = 10);
        ~PowerCollector();

        std::string name() const override;
        bool initialize() override;
        bool collect() override;
        void fill_snapshot(MetricSnapshot& snap) const override;
        void shutdown() override;

    private:
        // defining a struct to pass into the snapshot
        struct SupplyReading {
            bool discharging{ false };  // is discharging
            bool present{ true };       // is batter present, if missing default true
            bool device_scope{ false }; // if true skip peripheral devices
            bool online{ false };       // if charger is plugged in
            int32_t capacity{ -1 };     // battery charge left
            uint32_t cycle_count{ 0 };  // how many full charges done

            // in uW / uWh
            int64_t energy_now{ 0 };         // how much energy is in battery now
            int64_t energy_full{ 0 };        // what the battery energy is at full (degrades over time)
            int64_t energy_full_design{ 0 }; // what the battery energy was at full when new
            int64_t power_now{ 0 };          // current draw of power now, sometimes is negative so abs

            // in uA / uAh
            int64_t charge_now{ 0 };         // how much charge is in battery now
            int64_t charge_full{ 0 };        // what the battery charge is at full (degrades over time)
            int64_t charge_full_design{ 0 }; // what the battery charge was at full when new
            int64_t current_now{ 0 };        // current charge of power now, sometimes is negative so abs
            int64_t voltage_now{ 0 };        // needed for watts in charge style
        };

        struct Totals {
            bool any_battery{ false };
            bool any_charger_seen{ false };
            bool on_ac{ false };
            bool any_discharging{ false };
            bool watts_unknown{ false };
            int64_t sum_now{ 0 };
            int64_t sum_full{ 0 };
            int64_t sum_design{ 0 };
            int64_t sum_power{ 0 };
            int battery_count{ 0 };
            int32_t first_capacity{ -1 };
            uint32_t max_cycles{ 0 };
        };

        std::string m_base_path;
        int m_refresh_interval;

        ssize_t read_file(const std::string& path);
        void parse_uevent(char* buf, ssize_t n, SupplyReading& out);
        void refresh();

        int m_ticks_since_refresh{ 0 };
        std::vector<char> m_buf;
        MetricSnapshot::PowerState m_current;
    };
} // namespace pulsedb
