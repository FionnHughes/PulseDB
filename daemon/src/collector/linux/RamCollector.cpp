#include "RamCollector.h"
#include "../MetricSnapshot.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace pulsedb {

    RamCollector::RamCollector(std::string meminfo_path) : m_meminfo_path(std::move(meminfo_path)) {}

    RamCollector::~RamCollector() { shutdown(); }

    std::string RamCollector::name() const { return "ram"; }

    bool RamCollector::initialize() {
        m_file_handle = std::fopen(m_meminfo_path.c_str(), "r");

        if (!m_file_handle) {
            m_degraded = true;
            return false;
        }

        return true;
    }

    // reads /proc/meminfo and pulls relevent fields
    bool RamCollector::collect() {
        if (m_degraded) {
            return false;
        }

        std::rewind(m_file_handle);
        char line[256];
        LinuxMemInfo info{};
        while (fgets(line, sizeof(line), m_file_handle)) {
            char label[64];
            uint64_t value;
            if (sscanf(line, "%63s %" SCNu64, label, &value) == 2) {
                if (strcmp(label, "MemTotal:") == 0) {
                    info.mem_total = value;
                }
                else if (strcmp(label, "MemFree:") == 0) {
                    info.mem_free = value;
                }
                else if (strcmp(label, "MemAvailable:") == 0) {
                    info.mem_available = value;
                }
                else if (strcmp(label, "SwapTotal:") == 0) {
                    info.swap_total = value;
                }
                else if (strcmp(label, "SwapFree:") == 0) {
                    info.swap_free = value;
                }
                else if (strcmp(label, "Buffers:") == 0) {
                    info.buffers = value;
                }
                else if (strcmp(label, "Cached:") == 0) {
                    info.cached = value;
                }
            }
        }

        if (info.mem_total == 0) {
            return false;
        }

        m_ram_total_bytes = info.mem_total * 1024;
        m_ram_available_bytes = info.mem_available * 1024;
        m_ram_used_bytes = (info.mem_total - info.mem_available) * 1024;
        m_swap_total_bytes = info.swap_total * 1024;
        m_swap_used_bytes = (info.swap_total - info.swap_free) * 1024;
        m_page_cache_bytes = (info.cached + info.buffers) * 1024;

        return true;
    }

    void RamCollector::fill_snapshot(MetricSnapshot& snap) const {
        snap.ram_total_bytes = m_ram_total_bytes;
        snap.ram_available_bytes = m_ram_available_bytes;
        snap.ram_used_bytes = m_ram_used_bytes;
        snap.swap_total_bytes = m_swap_total_bytes;
        snap.swap_used_bytes = m_swap_used_bytes;
        snap.page_cache_bytes = m_page_cache_bytes;
    }

    void RamCollector::shutdown() {
        if (m_file_handle) {
            std::fclose(m_file_handle);
            m_file_handle = nullptr;
        }
    }
} // namespace pulsedb
