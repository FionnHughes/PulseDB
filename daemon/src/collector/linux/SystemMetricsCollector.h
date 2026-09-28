#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

#include "../IMetricCollector.h"
#include "../MetricSnapshot.h"

namespace pulsedb {

    // measures system metrics reading proc/stat and proc/vmstat
    class SystemMetricsCollector : public IMetricCollector {
    public:
        explicit SystemMetricsCollector(std::string stat_path = "/proc/stat", std::string vmstat_path = "/proc/vmstat");
        ~SystemMetricsCollector();

        SystemMetricsCollector(const SystemMetricsCollector&) = delete;
        SystemMetricsCollector& operator=(const SystemMetricsCollector&) = delete;

        std::string name() const override;
        bool initialize() override;
        bool collect() override;
        void fill_snapshot(MetricSnapshot& snap) const override;
        void shutdown() override;

    private:
        struct LinuxSystemCounters {
            // /proc/stat
            uint64_t ctxt{ 0 };          // cumulative, context switches
            uint64_t forks{ 0 };         // cumulative, processes/threads since boot
            uint32_t procs_running{ 0 }; // live gauge, processes runnning
            uint32_t procs_blocked{ 0 }; // live gauge, processes blocked

            // /proc/vmstat
            uint64_t pgfault{ 0 };    // cumulative, page faults
            uint64_t pgmajfault{ 0 }; // cumulative, how many major page faults
            uint64_t pswpin{ 0 };     // cumulative, pages swapped in
            uint64_t pswpout{ 0 };    // cumulative, pages swapped out
            uint64_t oom_kill{ 0 };   // cumulative, out of memory kills, stays 0 on kernels < 4.13
        };

        std::string m_stat_path;
        std::string m_vmstat_path;

        LinuxSystemCounters m_prev{};
        std::chrono::steady_clock::time_point m_prev_time;
        uint64_t m_page_size{ 0 };

        bool read_proc_stat(LinuxSystemCounters& out);
        bool read_proc_vmstat(LinuxSystemCounters& out);

        FILE* m_file_handle_stat{ nullptr };
        FILE* m_file_handle_vmstat{ nullptr };

        MetricSnapshot::SystemMetrics m_current;

        // set to true if init fails
        bool m_degraded{ false };
    };
} // namespace pulsedb
