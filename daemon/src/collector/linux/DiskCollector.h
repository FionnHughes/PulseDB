#pragma once
#include <chrono>
#include <optional>
#include <unordered_map>

#include "../IMetricCollector.h"
#include "../MetricSnapshot.h"

namespace pulsedb {

    bool is_excluded_device(const std::string& name);

    // reads disk info from proc/diskstats
    class DiskCollector : public IMetricCollector {
    public:
        explicit DiskCollector(std::string diskstats_path = "/proc/diskstats");
        ~DiskCollector();

        // prevents copying which would duplicate ownership of m_file_handle cause issues
        DiskCollector(const DiskCollector&) = delete;
        DiskCollector& operator=(const DiskCollector&) = delete;

        std::string name() const override;
        bool initialize() override;
        bool collect() override;
        void fill_snapshot(MetricSnapshot& snap) const override;
        void shutdown() override;

    private:
        struct LinuxDiskReading {
            std::string device_name;          // name of disk device
            uint64_t sectors_read{ 0 };       // cumulative since boot, x512 to bytes
            uint64_t sectors_written{ 0 };    // cumulative also
            uint64_t io_time_ms{ 0 };         // cumulative ms spent doing I/O, used for %util
            uint64_t discards_completed{ 0 }; // Linux-only, no Windows equivalent
            uint64_t sectors_discarded{ 0 };  // Linux-only, x512 for bytes
            uint64_t ios_in_progress{ 0 };    // not cumulative its live
        };

        std::string m_diskstats_path;
        FILE* m_file_handle{ nullptr };

        std::chrono::steady_clock::time_point m_last_collect_time;

        std::unordered_map<std::string, LinuxDiskReading> m_prev_disks;
        std::optional<std::vector<LinuxDiskReading>> read_proc_diskstats(FILE* file) const;
        std::vector<MetricSnapshot::DiskStats> m_disks;

        // set to true if init or collection fails
        bool m_degraded{ false };
    };
} // namespace pulsedb
