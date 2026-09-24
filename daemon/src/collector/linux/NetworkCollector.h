#pragma once
#include <chrono>
#include <optional>
#include <unordered_map>

#include "../IMetricCollector.h"
#include "../MetricSnapshot.h"

namespace pulsedb {
    // reads network info from proc/net/dev
    class NetworkCollector : public IMetricCollector {
    public:
        explicit NetworkCollector(std::string net_dev_path = "/proc/net/dev");
        ~NetworkCollector();

        // prevents copying which would duplicate ownership of m_file_handle cause issues
        NetworkCollector(const NetworkCollector&) = delete;
        NetworkCollector& operator=(const NetworkCollector&) = delete;

        std::string name() const override;
        bool initialize() override;
        bool collect() override;
        void fill_snapshot(MetricSnapshot& snap) const override;
        void shutdown() override;

    private:
        struct LinuxNetworkReading {
            std::string adapter_name; // interface name with trailing ':' (will be stripped)
            uint64_t rx_bytes{ 0 };   // cumulative since boot, amount of bytes received
            uint64_t rx_packets{ 0 }; // cumulative, received packets
            uint64_t rx_errors{ 0 };  // cumulative, reveived errors
            uint64_t rx_dropped{ 0 }; // cumulative, received drops

            uint64_t tx_bytes{ 0 };   // cumulative, transmit bytes
            uint64_t tx_packets{ 0 }; // cumulative, transmit packets
            uint64_t tx_errors{ 0 };  // cumulative, transmit errors
            uint64_t tx_dropped{ 0 }; // cumulative, transmit drops
        };

        std::string m_net_dev_path;
        FILE* m_file_handle{ nullptr };

        std::chrono::steady_clock::time_point m_last_collect_time;

        std::unordered_map<std::string, LinuxNetworkReading> m_prev_networks;
        std::optional<std::vector<LinuxNetworkReading>> read_proc_network(FILE* file) const;
        std::vector<MetricSnapshot::NetworkStats> m_networks;

        // set to true if init or collection fails
        bool m_degraded{ false };
    };
} // namespace pulsedb
