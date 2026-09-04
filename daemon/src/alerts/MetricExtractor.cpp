#include "MetricExtractor.h"

namespace pulsedb {

    std::optional<double> extract_metric_value(const MetricSnapshot& snap, const std::string& metric_name) {
        if (metric_name == "cpu_total_percent") return snap.cpu_total_percent;
        if (metric_name == "ram_used_bytes") return static_cast<double>(snap.ram_used_bytes);
        if (metric_name == "ram_available_bytes") return static_cast<double>(snap.ram_available_bytes);
        if (metric_name == "swap_used_bytes") return static_cast<double>(snap.swap_used_bytes);

        if (metric_name.rfind("cpu_core_", 0) == 0) {
            int idx = std::stoi(metric_name.substr(9));
            if (idx < 0 || idx >= (int)snap.cpu_per_core_percent.size()) return std::nullopt;
            return snap.cpu_per_core_percent[idx];
        }

        // disk_0_read, disk_0_write, disk_1_read... index just matches vector position
        if (metric_name.rfind("disk_", 0) == 0) {
            bool is_read = metric_name.find("_read") != std::string::npos;
            size_t idx_start = 5;
            size_t idx_end = metric_name.find(is_read ? "_read" : "_write");
            int idx = std::stoi(metric_name.substr(idx_start, idx_end - idx_start));
            if (idx < 0 || idx >= (int)snap.disks.size()) return std::nullopt;
            return static_cast<double>(is_read ? snap.disks[idx].read_bytes_per_sec : snap.disks[idx].write_bytes_per_sec);
        }

        // same deal, net_0_in, net_0_out
        if (metric_name.rfind("net_", 0) == 0) {
            bool is_in = metric_name.find("_in") != std::string::npos;
            size_t idx_start = 4;
            size_t idx_end = metric_name.find(is_in ? "_in" : "_out");
            int idx = std::stoi(metric_name.substr(idx_start, idx_end - idx_start));
            if (idx < 0 || idx >= (int)snap.network_adapters.size()) return std::nullopt;
            return static_cast<double>(is_in ? snap.network_adapters[idx].bytes_in_per_sec : snap.network_adapters[idx].bytes_out_per_sec);
        }

        return std::nullopt;
    }

}