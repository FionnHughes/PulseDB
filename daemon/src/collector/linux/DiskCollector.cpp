#include <cinttypes>
#include <string>

#include "DiskCollector.h"
#include "common/linux/Utils.h"

namespace pulsedb {
    DiskCollector::DiskCollector(std::string diskstats_path) : m_diskstats_path(std::move(diskstats_path)) {}

    DiskCollector::~DiskCollector() { shutdown(); }

    std::string DiskCollector::name() const { return "disk"; }

    std::optional<std::vector<DiskCollector::LinuxDiskReading>> DiskCollector::read_proc_diskstats(FILE* file) const {
        char line[256];
        std::vector<LinuxDiskReading> all_readings{};
        while (fgets(line, sizeof(line), file)) {
            LinuxDiskReading reading;
            char name_buf[32];
            // SCNu64 portable scanf format for for uint64_t
            if (sscanf(line,
                       "%*d %*d %31s %*" SCNu64 " %*" SCNu64 " %" SCNu64 " %*" SCNu64 " %*" SCNu64 " %*" SCNu64 " %" SCNu64 " %*" SCNu64 " %" SCNu64 " %" SCNu64
                       " %*" SCNu64 " %" SCNu64 " %*" SCNu64 " %" SCNu64 " %*" SCNu64,
                       name_buf, &reading.sectors_read, &reading.sectors_written, &reading.ios_in_progress, &reading.io_time_ms, &reading.discards_completed,
                       &reading.sectors_discarded) != 7) {
                return std::nullopt;
            }
            // here we are excluding the partitions which we dont need
            if (is_excluded_device(name_buf)) {
                continue;
            }
            reading.device_name = name_buf;
            all_readings.push_back(reading);
        }
        return all_readings;
    }

    bool is_excluded_device(const std::string& name) {
        // these are always virtual, never actual physical i/o so we skip them
        if (name.starts_with("loop") || name.starts_with("dm-") || name.starts_with("zram"))
            return true;

        // checks for a 'p' followed by a digit after some point in the name
        // e.g. nvme0n1p1 finds the 'p', sees '1', so it's a partition
        auto has_digit_after = [&](size_t search_from) {
            auto p = name.find('p', search_from);
            return p != std::string::npos && p + 1 < name.size() && std::isdigit(static_cast<unsigned char>(name[p + 1]));
        };

        // nvme/mmc/md partitions use the same diskname + p + number pattern
        // we are searching after the prefix
        if (name.starts_with("nvme"))
            return has_digit_after(4); // nvme0n1p1
        if (name.starts_with("mmcblk"))
            return has_digit_after(6); // mmcblk0p1
        if (name.starts_with("md"))
            return has_digit_after(2); // md0p1 (bare md0 is kept)

        // sd/vd/hd don't use a separator, partition number is added to the end
        // sda is a disk, sda1 is a partition not fully perfect check but sda is rare
        if (name.starts_with("sd") || name.starts_with("vd") || name.starts_with("hd"))
            return !name.empty() && std::isdigit(static_cast<unsigned char>(name.back()));

        // anything we don't recognise we can let through
        return false;
    }

    // sets the initial values of the disk to diff against
    bool DiskCollector::initialize() {
        m_file_handle = std::fopen(m_diskstats_path.c_str(), "r");
        if (!m_file_handle) {
            m_degraded = true;
            return false;
        }
        // setting the first collect time
        m_last_collect_time = std::chrono::steady_clock::now();

        auto first_read = read_proc_diskstats(m_file_handle);
        if (!first_read || first_read->empty()) {
            m_degraded = true;
            std::fclose(m_file_handle);
            m_file_handle = nullptr;
            return false;
        }

        // setting the unordered map of disk names and their stats
        for (const auto& reading : *first_read) {
            m_prev_disks[reading.device_name] = reading;
        }

        return true;
    }

    // collects proc/diskstats info
    bool DiskCollector::collect() {
        if (m_degraded) {
            return false;
        }
        auto now = std::chrono::steady_clock::now();
        // calculating the time since last collect for deltas
        double elapsed_sec = std::chrono::duration<double>(now - m_last_collect_time).count();
        m_last_collect_time = now;

        // always rewinding the file to the start before each collect
        std::rewind(m_file_handle);
        auto disk_readings = read_proc_diskstats(m_file_handle);
        if (!disk_readings || disk_readings->empty()) {
            return false;
        }

        m_disks.clear();
        m_disks.reserve(disk_readings->size());

        for (const auto& reading : *disk_readings) {
            // trying to find the current device in the past disk (for if a disk is just added and has no previous values to compare)
            auto it = m_prev_disks.find(reading.device_name);
            if (it == m_prev_disks.end()) {
                // still carry it forward so next tick has something to compare against
                m_prev_disks.emplace(reading.device_name, reading);
                continue;
            }
            const auto prev = it->second;
            it->second = reading;

            // creating the snapshot entry in correct units and formats
            MetricSnapshot::DiskStats entry;
            entry.device_name = reading.device_name;
            entry.read_bytes_per_sec = (reading.sectors_read - prev.sectors_read) * 512 / elapsed_sec;
            entry.write_bytes_per_sec = (reading.sectors_written - prev.sectors_written) * 512 / elapsed_sec;
            entry.utilization_percent = compute_percent(reading.io_time_ms - prev.io_time_ms, elapsed_sec * 1000);
            entry.queue_depth = reading.ios_in_progress;
            entry.discard_bytes_per_sec = (reading.sectors_discarded - prev.sectors_discarded) * 512 / elapsed_sec;
            entry.discards_per_sec = (reading.discards_completed - prev.discards_completed) / elapsed_sec;

            m_disks.push_back(entry);
        }

        return true;
    }

    void DiskCollector::fill_snapshot(MetricSnapshot& snap) const { snap.disks = m_disks; }

    void DiskCollector::shutdown() {
        if (m_file_handle) {
            std::fclose(m_file_handle);
            m_file_handle = nullptr;
        }
    }
} // namespace pulsedb
