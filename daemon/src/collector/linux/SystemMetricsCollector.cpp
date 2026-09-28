#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <utility>

#include "SystemMetricsCollector.h"

namespace pulsedb {

    SystemMetricsCollector::SystemMetricsCollector(std::string stat_path, std::string vmstat_path)
        : m_stat_path(std::move(stat_path)), m_vmstat_path(std::move(vmstat_path)) {}

    SystemMetricsCollector::~SystemMetricsCollector() { shutdown(); }

    std::string SystemMetricsCollector::name() const { return "system_metrics"; }

    bool SystemMetricsCollector::read_proc_stat(LinuxSystemCounters& out) {
        if (!m_file_handle_stat) {
            return false;
        }
        std::rewind(m_file_handle_stat);
        char line[4096];
        int found = 0;

        while (fgets(line, sizeof(line), m_file_handle_stat)) {
            // filtering for lines we want
            if (strncmp(line, "ctxt ", 5) == 0) {
                char* end = nullptr;
                out.ctxt = std::strtoull(line + 5, &end, 10);
                if (end == line + 5)
                    continue;
                found++;
            }
            else if (strncmp(line, "processes ", 10) == 0) {
                char* end = nullptr;
                out.forks = std::strtoull(line + 10, &end, 10);
                if (end == line + 10)
                    continue;
                found++;
            }
            else if (strncmp(line, "procs_running ", 14) == 0) {
                char* end = nullptr;
                out.procs_running = static_cast<uint32_t>(std::strtoull(line + 14, &end, 10));
                if (end == line + 14)
                    continue;
                found++;
            }
            else if (strncmp(line, "procs_blocked ", 14) == 0) {
                char* end = nullptr;
                out.procs_blocked = static_cast<uint32_t>(std::strtoull(line + 14, &end, 10));
                if (end == line + 14)
                    continue;
                found++;
            }
            if (found == 4) {
                break;
            }
        }
        return found == 4;
    }

    bool SystemMetricsCollector::read_proc_vmstat(LinuxSystemCounters& out) {
        if (!m_file_handle_vmstat) {
            return false;
        }
        std::rewind(m_file_handle_vmstat);
        char line[128];
        int mandatory_found = 0;
        bool oom_found = false;

        while (fgets(line, sizeof(line), m_file_handle_vmstat)) {
            // filtering for lines we want, we only want thing beginning with 'p' or 'o' easy check
            if (line[0] != 'p' && line[0] != 'o')
                continue;

            // finer filtering
            if (strncmp(line, "pgfault ", 8) == 0) {
                char* end = nullptr;
                out.pgfault = std::strtoull(line + 8, &end, 10);
                if (end == line + 8)
                    continue;
                mandatory_found++;
            }
            else if (strncmp(line, "pgmajfault ", 11) == 0) {
                char* end = nullptr;
                out.pgmajfault = std::strtoull(line + 11, &end, 10);
                if (end == line + 11)
                    continue;
                mandatory_found++;
            }
            else if (strncmp(line, "pswpin ", 7) == 0) {
                char* end = nullptr;
                out.pswpin = std::strtoull(line + 7, &end, 10);
                if (end == line + 7)
                    continue;
                mandatory_found++;
            }
            else if (strncmp(line, "pswpout ", 8) == 0) {
                char* end = nullptr;
                out.pswpout = std::strtoull(line + 8, &end, 10);
                if (end == line + 8)
                    continue;
                mandatory_found++;
            }
            else if (strncmp(line, "oom_kill ", 9) == 0) {
                char* end = nullptr;
                out.oom_kill = std::strtoull(line + 9, &end, 10);
                if (end == line + 9)
                    continue;
                oom_found = true;
            }
            if (mandatory_found == 4 && oom_found) {
                break;
            }
        }
        return mandatory_found == 4;
    }

    // initializes for the first value to delta against
    bool SystemMetricsCollector::initialize() {

        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0) {
            m_degraded = true;
            return false;
        }
        m_page_size = static_cast<uint64_t>(page_size);

        // this time tracking two files
        m_file_handle_stat = std::fopen(m_stat_path.c_str(), "r");
        m_file_handle_vmstat = std::fopen(m_vmstat_path.c_str(), "r");

        if (!m_file_handle_stat || !m_file_handle_vmstat) {
            m_degraded = true;
            shutdown();
            return false;
        }

        if (!read_proc_stat(m_prev) || !read_proc_vmstat(m_prev)) {
            m_degraded = true;
            shutdown();
            return false;
        }
        m_prev_time = std::chrono::steady_clock::now();
        return true;
    }

    // samples system info, computes deltas from last tick
    bool SystemMetricsCollector::collect() {
        if (m_degraded) {
            return false;
        }

        LinuxSystemCounters cur{};

        if (!read_proc_stat(cur) || !read_proc_vmstat(cur)) {
            return false;
        }
        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - m_prev_time).count();

        if (elapsed_sec <= 0) {
            return false;
        }

        // rounding properly as static cast uin64_t will alwasy round down
        m_current.context_switches_per_sec = static_cast<uint64_t>(std::llround((cur.ctxt - m_prev.ctxt) / elapsed_sec));
        m_current.forks_per_sec = static_cast<uint64_t>(std::llround((cur.forks - m_prev.forks) / elapsed_sec));
        m_current.page_faults_per_sec = static_cast<uint64_t>(std::llround((cur.pgfault - m_prev.pgfault) / elapsed_sec));
        m_current.major_page_faults_per_sec = static_cast<uint64_t>(std::llround((cur.pgmajfault - m_prev.pgmajfault) / elapsed_sec));
        m_current.swap_in_bytes_per_sec = static_cast<uint64_t>(std::llround((cur.pswpin - m_prev.pswpin) * m_page_size / elapsed_sec));
        m_current.swap_out_bytes_per_sec = static_cast<uint64_t>(std::llround((cur.pswpout - m_prev.pswpout) * m_page_size / elapsed_sec));
        m_current.procs_running = cur.procs_running;
        m_current.procs_blocked = cur.procs_blocked;
        m_current.oom_kills_total = cur.oom_kill;

        m_prev = cur;
        m_prev_time = now;

        return true;
    }

    // copies the struct into the shared snapshot
    void SystemMetricsCollector::fill_snapshot(MetricSnapshot& snap) const { snap.system_metrics = m_current; }

    void SystemMetricsCollector::shutdown() {
        if (m_file_handle_stat) {
            std::fclose(m_file_handle_stat);
            m_file_handle_stat = nullptr;
        }
        if (m_file_handle_vmstat) {
            std::fclose(m_file_handle_vmstat);
            m_file_handle_vmstat = nullptr;
        }
    }
} // namespace pulsedb
