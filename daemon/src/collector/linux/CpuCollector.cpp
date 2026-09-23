#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <utility>

#include "../MetricSnapshot.h"
#include "CpuCollector.h"
#include "common/Utils.h"

namespace pulsedb {

    CpuCollector::CpuCollector(std::string stat_path) : m_stat_path(std::move(stat_path)) {}

    CpuCollector::~CpuCollector() { shutdown(); }

    std::string CpuCollector::name() const { return "cpu"; }

    std::optional<CpuCollector::LinuxCpu> CpuCollector::read_proc_stat(FILE* file) const {
        char line[256];
        LinuxCpu times{};
        while (fgets(line, sizeof(line), file)) {
            // cpu info starts with cpu, either cpu / cpu0,cpu1 etc
            if (strncmp(line, "cpu", 3) == 0) {
                // if aggregated cpu data
                if (strncmp(line, "cpu ", 4) == 0) {
                    if (sscanf(line,
                               "cpu %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                               " %" SCNu64 " %" SCNu64 " %" SCNu64,
                               &times.total.user, &times.total.nice, &times.total.system, &times.total.idle,
                               &times.total.iowait, &times.total.irq, &times.total.softirq, &times.total.steal,
                               &times.total.guest, &times.total.guest_nice) < 5) {
                        // proc/stats can have less than 10 values if its a legacy system etc but that isnt an issue
                        // unless we only return so few which indicates theres an issue somewhere which is why we check
                        return std::nullopt;
                    }
                }
                else {
                    int idx;
                    LinuxCpuTimes core_times{};
                    if (sscanf(line,
                               "cpu%d %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                               " %" SCNu64 " %" SCNu64 " %" SCNu64,
                               &idx, &core_times.user, &core_times.nice, &core_times.system, &core_times.idle,
                               &core_times.iowait, &core_times.irq, &core_times.softirq, &core_times.steal,
                               &core_times.guest, &core_times.guest_nice) < 5) {
                        return std::nullopt;
                    }
                    // defensive check against idx returned and cores already collected
                    if (static_cast<size_t>(idx) == times.per_core.size()) {
                        times.per_core.push_back(core_times);
                    }
                }
            }
            else {
                break;
            }
        }
        return times;
    }
    uint64_t CpuCollector::compute_total_delta(const LinuxCpuTimes& prev, const LinuxCpuTimes& curr) {
        uint64_t delta_user = curr.user - prev.user;
        uint64_t delta_nice = curr.nice - prev.nice;
        uint64_t delta_system = curr.system - prev.system;
        uint64_t delta_idle = curr.idle - prev.idle;
        uint64_t delta_iowait = curr.iowait - prev.iowait;
        uint64_t delta_irq = curr.irq - prev.irq;
        uint64_t delta_softirq = curr.softirq - prev.softirq;
        uint64_t delta_steal = curr.steal - prev.steal;

        uint64_t total_delta = delta_user + delta_nice + delta_system + delta_idle + delta_iowait + delta_irq +
                               delta_softirq + delta_steal;
        return total_delta;
    }

    // gets the core count, sizes all the vectors, seeds the previous tick
    // values for the first collect
    bool CpuCollector::initialize() {

        m_file_handle = std::fopen(m_stat_path.c_str(), "r");

        if (!m_file_handle) {
            m_degraded = true;
            return false;
        }

        m_prev_cpu = read_proc_stat(m_file_handle);
        if (!m_prev_cpu) {
            m_degraded = true;
            std::fclose(m_file_handle);
            m_file_handle = nullptr;
            return false;
        }
        m_core_count = static_cast<int>(m_prev_cpu->per_core.size());
        if (m_core_count == 0) {
            m_degraded = true;
            std::fclose(m_file_handle);
            m_file_handle = nullptr;
            return false;
        }

        m_per_core_percent.resize(m_core_count);
        m_per_core_freq_mhz.resize(m_core_count);

        return true;
    }

    // samples system times, computes deltas from last tick, calculates usage
    // percentages for total and per core
    bool CpuCollector::collect() {
        if (m_degraded) {
            return false;
        }

        std::rewind(m_file_handle);
        std::optional<LinuxCpu> cpu = read_proc_stat(m_file_handle);
        if (!cpu || cpu->per_core.size() != static_cast<std::size_t>(m_core_count)) {
            return false;
        }

        uint64_t total_delta = compute_total_delta(m_prev_cpu->total, cpu->total);
        uint64_t busy_delta =
            total_delta - (cpu->total.idle - m_prev_cpu->total.idle) - (cpu->total.iowait - m_prev_cpu->total.iowait);

        uint64_t iowait_delta = cpu->total.iowait - m_prev_cpu->total.iowait;
        uint64_t steal_delta = cpu->total.steal - m_prev_cpu->total.steal;

        m_cpu_total_percent = compute_percent(busy_delta, total_delta);
        m_cpu_iowait_percent = compute_percent(iowait_delta, total_delta);
        m_cpu_steal_percent = compute_percent(steal_delta, total_delta);

        for (int i = 0; i < m_core_count; i++) {
            uint64_t core_total_delta = compute_total_delta(m_prev_cpu->per_core[i], cpu->per_core[i]);

            uint64_t core_busy_delta = core_total_delta - (cpu->per_core[i].idle - m_prev_cpu->per_core[i].idle) -
                                       (cpu->per_core[i].iowait - m_prev_cpu->per_core[i].iowait);
            m_per_core_percent[i] = compute_percent(core_busy_delta, core_total_delta);
        }
        m_prev_cpu = std::move(cpu);

        return true;
    }

    // copies the cached per core and total percentages into the shared snapshot
    void CpuCollector::fill_snapshot(MetricSnapshot& snap) const {
        snap.cpu_total_percent = m_cpu_total_percent;
        snap.cpu_per_core_percent = m_per_core_percent;
        snap.cpu_per_core_frequency_mhz = m_per_core_freq_mhz;
        snap.cpu_iowait_percent = m_cpu_iowait_percent;
        snap.cpu_steal_percent = m_cpu_steal_percent;
    }

    void CpuCollector::shutdown() {
        if (m_file_handle) {
            std::fclose(m_file_handle);
            m_file_handle = nullptr;
        }
    }
} // namespace pulsedb
