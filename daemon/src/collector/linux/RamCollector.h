#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "../IMetricCollector.h"

namespace pulsedb {
    // reads meminfo from /proc/meminfo
    class RamCollector : public IMetricCollector {
    public:
        explicit RamCollector(std::string meminfo_path = "/proc/meminfo");
        ~RamCollector();

        // prevents copying which would duplicate ownership of m_file_handle cause issues
        RamCollector(const RamCollector&) = delete;
        RamCollector& operator=(const RamCollector&) = delete;

        std::string name() const override;
        bool initialize() override;
        bool collect() override;
        void fill_snapshot(MetricSnapshot& snap) const override;
        void shutdown() override;

    private:
        // defining a struct to pass into the snapshot all in kB
        struct LinuxMemInfo {
            uint64_t mem_total{ 0 };     // total physical RAM
            uint64_t mem_free{ 0 };      // completely untouched RAM, kB (different to "available")
            uint64_t mem_available{ 0 }; // estimate of RAM a new app can get without swapping
            uint64_t swap_total{ 0 };    // total configured swap space
            uint64_t swap_free{ 0 };     // unused swap space
            uint64_t buffers{ 0 };       // disk block metadata cache
            uint64_t cached{ 0 };        // file data page cache (buffers + cached is "free" RAM)
        };

        std::string m_meminfo_path;
        FILE* m_file_handle{ nullptr };

        uint64_t m_ram_total_bytes{ 0 };
        uint64_t m_ram_used_bytes{ 0 };
        uint64_t m_ram_available_bytes{ 0 };
        uint64_t m_swap_total_bytes{ 0 };
        uint64_t m_swap_used_bytes{ 0 };
        uint64_t m_page_cache_bytes{ 0 };

        // set to true if init or collection fails
        bool m_degraded{ false };
    };
} // namespace pulsedb
