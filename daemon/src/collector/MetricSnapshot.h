#pragma once

#include <cstdint>
#include <string>
#include <vector>


namespace pulsedb {

	struct MetricSnapshot {
		int64_t timestamp_ms{ 0 };

		float cpu_total_percent{ 0.0f };
		std::vector<float> cpu_per_core_percent;
		std::vector<float> cpu_per_core_frequency_mhz;
		
		uint64_t ram_total_bytes{ 0 };
		uint64_t ram_used_bytes{ 0 };
		uint64_t ram_available_bytes{ 0 };
		uint64_t swap_total_bytes{ 0 };
		uint64_t swap_used_bytes{ 0 };

		struct DiskStats {
			std::string device_name;
			uint64_t read_bytes_per_sec{ 0 };
			uint64_t write_bytes_per_sec{ 0 };
			float utilization_percent{ 0.0f };
			uint64_t queue_depth{ 0 };
		};

		struct NetworkStats {
			std::string adapter_name;
			uint64_t bytes_in_per_sec{ 0 };
			uint64_t bytes_out_per_sec{ 0 };
			uint64_t packets_in_per_sec{ 0 };
			uint64_t packets_out_per_sec{ 0 };
		};

		struct ProcessInfo {
			uint32_t pid{ 0 };
			std::string name;
			float cpu_percent{ 0.0f };
			uint64_t ram_bytes{ 0 };
			uint64_t disk_read_bytes_per_sec{ 0 };
			uint64_t disk_write_bytes_per_sec{ 0 };
		};

		std::vector<DiskStats> disks;
		std::vector<NetworkStats> network_adapters;
		std::vector<ProcessInfo> top_processes;

		void reserve(int core_count, int disk_count, int adapter_count, int process_count) {
			cpu_per_core_percent.reserve(core_count);
			cpu_per_core_frequency_mhz.reserve(core_count);
			disks.reserve(disk_count);
			network_adapters.reserve(adapter_count);
			top_processes.reserve(process_count);
		}
	};
}