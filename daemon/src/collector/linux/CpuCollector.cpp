#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <utility>

#include "../MetricSnapshot.h"
#include "CpuCollector.h"
#include "common/linux/Utils.h"

namespace pulsedb {

    CpuCollector::CpuCollector(std::string stat_path) : m_stat_path(std::move(stat_path)) {}

    CpuCollector::~CpuCollector() { shutdown(); }

    std::string CpuCollector::name() const { return "cpu"; }

    bool CpuCollector::read_proc_stat(LinuxCpu& out) {
        out.per_core.clear();
        out.total = {};

        ssize_t n = read_whole_file(m_fd, m_buf);
        if (n < 0) {
            return false;
        }
        char* line = m_buf.data();
        char* buf_end = line + n;
        while (line < buf_end) {
            // split buffer into lines manually and null terminate each so sscanf only sees one line
            char* nl = static_cast<char*>(std::memchr(line, '\n', buf_end - line));
            if (nl) {
                *nl = '\0';
            }
            char* cur = line;
            line = nl ? nl + 1 : buf_end;
            // cpu info starts with cpu, either cpu / cpu0,cpu1 etc
            if (strncmp(cur, "cpu", 3) == 0) {
                // if aggregated cpu data
                if (strncmp(cur, "cpu ", 4) == 0) {
                    if (sscanf(cur, "cpu %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                               &out.total.user, &out.total.nice, &out.total.system, &out.total.idle, &out.total.iowait, &out.total.irq, &out.total.softirq,
                               &out.total.steal, &out.total.guest, &out.total.guest_nice) < 5) {
                        // proc/stats can have less than 10 values if its a legacy system etc but that isnt an issue
                        // unless we only return so few which indicates theres an issue somewhere which is why we check
                        return false;
                    }
                }
                else {
                    int idx;
                    LinuxCpuTimes core_times{};
                    if (sscanf(cur,
                               "cpu%d %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                               &idx, &core_times.user, &core_times.nice, &core_times.system, &core_times.idle, &core_times.iowait, &core_times.irq,
                               &core_times.softirq, &core_times.steal, &core_times.guest, &core_times.guest_nice) < 5) {
                        return false;
                    }
                    // defensive check against idx returned and cores already collected
                    if (static_cast<size_t>(idx) == out.per_core.size()) {
                        out.per_core.push_back(core_times);
                    }
                }
            }
            else {
                break;
            }
        }
        return true;
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

        uint64_t total_delta = delta_user + delta_nice + delta_system + delta_idle + delta_iowait + delta_irq + delta_softirq + delta_steal;
        return total_delta;
    }

    // gets the core count, sizes all the vectors, seeds the previous tick
    // values for the first collect
    bool CpuCollector::initialize() {
        m_buf.resize(16384);

        m_fd = open(m_stat_path.c_str(), O_RDONLY | O_CLOEXEC);

        if (m_fd < 0) {
            m_degraded = true;
            return false;
        }

        if (!read_proc_stat(m_prev_cpu)) {
            m_degraded = true;
            shutdown();
            return false;
        }
        m_core_count = static_cast<int>(m_prev_cpu.per_core.size());
        if (m_core_count == 0) {
            m_degraded = true;
            shutdown();
            return false;
        }

        m_cur_cpu.per_core.reserve(m_core_count);
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

        if (!read_proc_stat(m_cur_cpu) || m_cur_cpu.per_core.size() != static_cast<size_t>(m_core_count)) {
            return false;
        }

        uint64_t total_delta = compute_total_delta(m_prev_cpu.total, m_cur_cpu.total);
        uint64_t busy_delta = total_delta - (m_cur_cpu.total.idle - m_prev_cpu.total.idle) - (m_cur_cpu.total.iowait - m_prev_cpu.total.iowait);

        uint64_t iowait_delta = m_cur_cpu.total.iowait - m_prev_cpu.total.iowait;
        uint64_t steal_delta = m_cur_cpu.total.steal - m_prev_cpu.total.steal;

        m_cpu_total_percent = compute_percent(busy_delta, total_delta);
        m_cpu_iowait_percent = compute_percent(iowait_delta, total_delta);
        m_cpu_steal_percent = compute_percent(steal_delta, total_delta);

        for (int i = 0; i < m_core_count; i++) {
            uint64_t core_total_delta = compute_total_delta(m_prev_cpu.per_core[i], m_cur_cpu.per_core[i]);

            uint64_t core_busy_delta =
                core_total_delta - (m_cur_cpu.per_core[i].idle - m_prev_cpu.per_core[i].idle) - (m_cur_cpu.per_core[i].iowait - m_prev_cpu.per_core[i].iowait);
            m_per_core_percent[i] = compute_percent(core_busy_delta, core_total_delta);
        }
        std::swap(m_prev_cpu, m_cur_cpu);

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
        if (m_fd >= 0) {
            close(m_fd);
            m_fd = -1;
        }
    }
} // namespace pulsedb
