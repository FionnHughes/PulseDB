#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "../IMetricCollector.h"

namespace pulsedb {

    // measures total and per core CPU usage using Linux's /proc/stat
    class CpuCollector : public IMetricCollector {
    public:
        explicit CpuCollector(std::string stat_path = "/proc/stat");
        ~CpuCollector();

        CpuCollector(const CpuCollector&) = delete;
        CpuCollector& operator=(const CpuCollector&) = delete;

        std::string name() const override;
        bool initialize() override;
        bool collect() override;
        void fill_snapshot(MetricSnapshot& snap) const override;
        void shutdown() override;

    private:
        // defining a custom linux struct rather than dealing with windows set query formats
        struct LinuxCpuTimes {
            uint64_t user{ 0 };       // time spent running normal user processes
            uint64_t nice{ 0 };       // same as user, but for low-priority "niced" processes
            uint64_t system{ 0 };     // time spent running kernel code
            uint64_t idle{ 0 };       // time spent doing nothing
            uint64_t iowait{ 0 };     // time spent idle while waiting on disk/network I/O
                                      // this can be unreliable, especially per core
            uint64_t irq{ 0 };        // time spent servicing hardware interrupts
            uint64_t softirq{ 0 };    // time spent servicing software interrupts
            uint64_t steal{ 0 };      // time stolen by the hypervisor for other VMs
            uint64_t guest{ 0 };      // time spent running a virtual CPU for a guest OS
            uint64_t guest_nice{ 0 }; // same as guest, but for a niced guest process
        };

        struct LinuxCpu {
            LinuxCpuTimes total;
            std::vector<LinuxCpuTimes> per_core;
        };

        std::string m_stat_path;

        int m_core_count{ 0 };

        // previous tick values for total CPU calculation
        std::optional<LinuxCpu> m_prev_cpu;

        std::optional<CpuCollector::LinuxCpu> read_proc_stat(FILE* file) const;
        static uint64_t compute_total_delta(const LinuxCpuTimes& prev, const LinuxCpuTimes& curr);
        static float compute_percent(uint64_t part_delta, uint64_t total_delta);

        float m_cpu_total_percent{ 0.0f };
        float m_cpu_iowait_percent{ 0.0f };
        float m_cpu_steal_percent{ 0.0f };

        std::vector<float> m_per_core_percent;
        // will be empty for now as the timings are very variable by miliseconds
        std::vector<float> m_per_core_freq_mhz;

        FILE* m_file_handle{ nullptr };

        // set to true if init or collection fails, if collect() returns false and then stop trying
        bool m_degraded{ false };
    };
} // namespace pulsedb
