#include "PowerCollector.h"
#include "collector/MetricSnapshot.h"
#include "common/linux/Utils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace pulsedb {

    PowerCollector::PowerCollector(std::string base_path, int refresh_interval) : m_base_path(std::move(base_path)), m_refresh_interval(refresh_interval) {}

    PowerCollector::~PowerCollector() { shutdown(); }

    std::string PowerCollector::name() const { return "power"; }

    // fill m_current
    bool PowerCollector::initialize() {
        m_buf.resize(4096);
        m_refresh_interval = (m_refresh_interval >= 1) ? m_refresh_interval : 1;

        refresh();
        m_ticks_since_refresh = 0;

        return true;
    }

    ssize_t PowerCollector::read_file(const std::string& path) {
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            return -1;
        }

        ssize_t n = read_whole_file(fd, m_buf);
        close(fd);
        if (n < 0) {
            return -1;
        }
        return n;
    }

    void PowerCollector::parse_uevent(char* buf, ssize_t n, SupplyReading& out) {
        char* line = buf;
        char* buf_end = buf + n;

        while (line < buf_end) {
            // I'm spliting buffer into lines manually, line jumps to the next one before matching
            char* nl = static_cast<char*>(std::memchr(line, '\n', buf_end - line));
            if (nl) {
                *nl = '\0';
            }
            char* cur = line;
            line = nl ? nl + 1 : buf_end;

            if (std::strncmp(cur, "POWER_SUPPLY_", 13) != 0)
                continue;

            // split into key and value at the '='
            char* key = cur + 13;
            char* eq = std::strchr(key, '=');
            if (!eq)
                continue;
            *eq = '\0';
            char* value = eq + 1;

            if (std::strcmp(key, "STATUS") == 0) {
                out.discharging = std::strcmp(value, "Discharging") == 0;
            }
            else if (std::strcmp(key, "PRESENT") == 0) {
                out.present = value[0] == '1';
            }
            else if (std::strcmp(key, "SCOPE") == 0) {
                out.device_scope = std::strcmp(value, "Device") == 0;
            }
            else if (std::strcmp(key, "ONLINE") == 0) {
                out.online = value[0] == '1';
            }
            else if (std::strcmp(key, "CAPACITY") == 0) {
                out.capacity = static_cast<int32_t>(std::strtoll(value, nullptr, 10));
            }
            else if (std::strcmp(key, "CYCLE_COUNT") == 0) {
                out.cycle_count = static_cast<uint32_t>(std::strtoll(value, nullptr, 10));
            }
            else if (std::strcmp(key, "ENERGY_NOW") == 0) {
                out.energy_now = std::strtoll(value, nullptr, 10);
            }
            else if (std::strcmp(key, "ENERGY_FULL") == 0) {
                out.energy_full = std::strtoll(value, nullptr, 10);
            }
            else if (std::strcmp(key, "ENERGY_FULL_DESIGN") == 0) {
                out.energy_full_design = std::strtoll(value, nullptr, 10);
            }
            else if (std::strcmp(key, "POWER_NOW") == 0) {
                // using absolute as some computers show negative when discharging
                out.power_now = std::llabs(std::strtoll(value, nullptr, 10));
            }
            else if (std::strcmp(key, "CHARGE_NOW") == 0) {
                out.charge_now = std::strtoll(value, nullptr, 10);
            }
            else if (std::strcmp(key, "CHARGE_FULL") == 0) {
                out.charge_full = std::strtoll(value, nullptr, 10);
            }
            else if (std::strcmp(key, "CHARGE_FULL_DESIGN") == 0) {
                out.charge_full_design = std::strtoll(value, nullptr, 10);
            }
            else if (std::strcmp(key, "CURRENT_NOW") == 0) {
                out.current_now = std::llabs(std::strtoll(value, nullptr, 10));
            }
            else if (std::strcmp(key, "VOLTAGE_NOW") == 0) {
                out.voltage_now = std::strtoll(value, nullptr, 10);
            }
        }
    }

    // rescan every time so any chargers plugged in after startup get picked up
    void PowerCollector::refresh() {
        Totals t{};
        DIR* dir = opendir(m_base_path.c_str());
        if (dir) {
            while (dirent* entry = readdir(dir)) {
                const char* name = entry->d_name;
                if (name[0] == '.') {
                    continue;
                }
                std::string supply = m_base_path + "/" + name;
                ssize_t n = read_file(supply + "/type");
                if (n < 0) {
                    continue;
                }
                if (n > 0 && m_buf[n - 1] == '\n') {
                    m_buf[n - 1] = '\0';
                }
                // work this out now before m_buf is gone
                bool is_battery = strcmp(m_buf.data(), "Battery") == 0;
                SupplyReading s{};
                n = read_file(supply + "/uevent");
                if (n < 0) {
                    continue;
                }
                parse_uevent(m_buf.data(), n, s);
                // anything thats not a battery is a charger and online means on ac
                if (!is_battery) {
                    t.any_charger_seen = true;
                    if (s.online) {
                        t.on_ac = true;
                    }
                    continue;
                }
                // skip other devices and empy batteries
                if (s.device_scope || !s.present) {
                    continue;
                }

                t.any_battery = true;
                t.battery_count++;

                if (t.battery_count == 1) {
                    t.first_capacity = s.capacity;
                }
                t.max_cycles = std::max(t.max_cycles, s.cycle_count);

                if (s.discharging) {
                    t.any_discharging = true;
                }

                // batteries report either energy or charge and we convert to energy so multiple can be summed
                int64_t now = 0, full = 0, design = 0, power = 0;

                if (s.energy_full > 0) {
                    // energy, already in uWh / uW
                    now = s.energy_now;
                    full = s.energy_full;
                    design = s.energy_full_design;
                    power = s.power_now;
                }
                else if (s.charge_full > 0 && s.voltage_now > 0) {
                    // charge, convert to uWh / uW using voltage
                    now = s.charge_now * s.voltage_now / 1000000;
                    full = s.charge_full * s.voltage_now / 1000000;
                    design = s.charge_full_design * s.voltage_now / 1000000;
                    power = s.current_now * s.voltage_now / 1000000;
                }
                else if (s.charge_full > 0) {
                    // charge with no voltage, ratios work
                    now = s.charge_now;
                    full = s.charge_full;
                    design = s.charge_full_design;
                    power = s.current_now;
                    t.watts_unknown = true;
                }
                t.sum_now += now;
                t.sum_full += full;
                t.sum_design += design;
                t.sum_power += power;
            }
            closedir(dir);
        }
        MetricSnapshot::PowerState p{};
        p.battery_present = t.any_battery;
        // deciding whether laptop / desktop and if on ac
        if (!t.any_battery) {
            p.on_ac_power = true;
        }
        else if (t.any_charger_seen) {
            p.on_ac_power = t.on_ac;
        }
        else {
            p.on_ac_power = !t.any_discharging;
        }

        // determining battery percent, 255 is unknown
        p.battery_percent = 255;
        if (t.any_battery) {
            long long pct = -1;
            if (t.battery_count == 1 && t.first_capacity >= 0) {
                pct = t.first_capacity;
            }
            else if (t.sum_full > 0) {
                pct = std::llround(t.sum_now * 100.0 / t.sum_full);
            }

            if (pct >= 0) {
                p.battery_percent = static_cast<uint8_t>(std::clamp(pct, 0LL, 100LL));
            }
        }

        // anything over a week is just wrong
        constexpr double MAX_BATTERY_SECS = 7.0 * 24 * 3600;

        p.battery_seconds_remaining = -1;
        p.battery_full_seconds_remaining = -1;

        if (t.any_battery && t.any_discharging && t.sum_power > 0) {
            double power = static_cast<double>(t.sum_power);
            double secs = t.sum_now / power * 3600.0;
            double full_secs = t.sum_full / power * 3600.0;

            if (secs <= MAX_BATTERY_SECS) {
                p.battery_seconds_remaining = static_cast<int32_t>(std::llround(secs));
            }
            if (full_secs <= MAX_BATTERY_SECS) {
                p.battery_full_seconds_remaining = static_cast<int32_t>(std::llround(full_secs));
            }
        }

        if (t.any_battery && !t.watts_unknown) {
            p.battery_power_watts = static_cast<float>(t.sum_power / 1e6);
        }

        if (t.sum_design > 0) {
            p.battery_health_percent = static_cast<float>(t.sum_full * 100.0 / t.sum_design);
        }

        p.battery_cycle_count = t.max_cycles;

        m_current = p;
    }

    // only rereads at intervals
    bool PowerCollector::collect() {
        m_ticks_since_refresh++;
        if (m_ticks_since_refresh >= m_refresh_interval) {
            refresh();
            m_ticks_since_refresh = 0;
        }
        return true;
    }

    void PowerCollector::fill_snapshot(MetricSnapshot& snap) const { snap.power = m_current; }

    // nothing held open to close
    void PowerCollector::shutdown() {}
} // namespace pulsedb
