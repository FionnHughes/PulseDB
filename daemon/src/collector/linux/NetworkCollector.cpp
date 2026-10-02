#include <cinttypes>
#include <string>

#include "NetworkCollector.h"

namespace pulsedb {
    NetworkCollector::NetworkCollector(std::string net_dev_path) : m_net_dev_path(std::move(net_dev_path)) {}

    NetworkCollector::~NetworkCollector() { shutdown(); }

    std::string NetworkCollector::name() const { return "network"; }

    std::optional<std::vector<NetworkCollector::LinuxNetworkReading>> NetworkCollector::read_proc_network(FILE* file) const {
        char line[256];
        std::vector<LinuxNetworkReading> all_readings{};
        while (fgets(line, sizeof(line), file)) {
            LinuxNetworkReading reading;
            char name_buf[32];
            // SCNu64 portable scanf format for for uint64_t
            if (sscanf(line,
                       "%31s %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %*" SCNu64 " %*" SCNu64 " %*" SCNu64 " %*" SCNu64 " %" SCNu64 " %" SCNu64
                       " %" SCNu64 " %" SCNu64 " %*" SCNu64 " %*" SCNu64 " %*" SCNu64 " %*" SCNu64,
                       name_buf, &reading.rx_bytes, &reading.rx_packets, &reading.rx_errors, &reading.rx_dropped, &reading.tx_bytes, &reading.tx_packets,
                       &reading.tx_errors, &reading.tx_dropped) != 9) {
                // continuing not breaking because if it fails it most likely the header
                continue;
            }
            std::string name = name_buf;
            if (!name.empty() && name.back() == ':') {
                name.pop_back();
            }
            // here we are excluding local loopback
            if (name == "lo") {
                continue;
            }
            reading.adapter_name = name;
            all_readings.push_back(reading);
        }
        return all_readings;
    }

    // sets the initial values of the network stats to diff against
    bool NetworkCollector::initialize() {
        m_file_handle = std::fopen(m_net_dev_path.c_str(), "r");
        if (!m_file_handle) {
            m_degraded = true;
            return false;
        }
        // setting the first collect time
        m_last_collect_time = std::chrono::steady_clock::now();

        auto first_read = read_proc_network(m_file_handle);
        if (!first_read || first_read->empty()) {
            m_degraded = true;
            std::fclose(m_file_handle);
            m_file_handle = nullptr;
            return false;
        }

        // setting the unordered map of network names and their stats
        for (const auto& reading : *first_read) {
            m_prev_networks[reading.adapter_name] = reading;
        }

        return true;
    }

    // collects proc/net/dev info
    bool NetworkCollector::collect() {
        if (m_degraded) {
            return false;
        }
        auto now = std::chrono::steady_clock::now();
        // calculating the time since last collect for deltas
        double elapsed_sec = std::chrono::duration<double>(now - m_last_collect_time).count();
        m_last_collect_time = now;

        // always rewinding the file to the start before each collect
        std::rewind(m_file_handle);
        auto network_readings = read_proc_network(m_file_handle);
        if (!network_readings || network_readings->empty()) {
            return false;
        }

        m_networks.clear();
        m_networks.reserve(network_readings->size());

        for (const auto& reading : *network_readings) {
            // trying to find the current network in the past networks (for if a network is just added and has no previous values to compare)
            auto it = m_prev_networks.find(reading.adapter_name);
            if (it == m_prev_networks.end()) {
                // still carry it forward so next tick has something to compare against
                m_prev_networks.emplace(reading.adapter_name, reading);
                continue;
            }
            const auto prev = it->second;
            it->second = reading;

            // creating the snapshot entry in correct units and formats
            MetricSnapshot::NetworkStats entry;
            entry.adapter_name = reading.adapter_name;
            entry.bytes_in_per_sec = (reading.rx_bytes - prev.rx_bytes) / elapsed_sec;
            entry.bytes_out_per_sec = (reading.tx_bytes - prev.tx_bytes) / elapsed_sec;
            entry.packets_in_per_sec = (reading.rx_packets - prev.rx_packets) / elapsed_sec;
            entry.packets_out_per_sec = (reading.tx_packets - prev.tx_packets) / elapsed_sec;
            entry.rx_dropped_per_sec = (reading.rx_dropped - prev.rx_dropped) / elapsed_sec;
            entry.tx_dropped_per_sec = (reading.tx_dropped - prev.tx_dropped) / elapsed_sec;
            entry.rx_errors_per_sec = (reading.rx_errors - prev.rx_errors) / elapsed_sec;
            entry.tx_errors_per_sec = (reading.tx_errors - prev.tx_errors) / elapsed_sec;

            m_networks.push_back(entry);
        }

        return true;
    }

    void NetworkCollector::fill_snapshot(MetricSnapshot& snap) const { snap.network_adapters = m_networks; }

    void NetworkCollector::shutdown() {
        if (m_file_handle) {
            std::fclose(m_file_handle);
            m_file_handle = nullptr;
        }
    }
} // namespace pulsedb
