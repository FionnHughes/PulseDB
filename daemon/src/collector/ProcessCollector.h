#pragma once

#include <vector>
#include <unordered_map>
#include <windows.h>

#include "IMetricCollector.h"
#include "MetricSnapshot.h"
#include "NtdllHelper.h"

namespace pulsedb {
	// uses NtQuerySystemInformation to get the top 25 processes by CPU usage and also tracks pulsedb's own CPU and RAM
	class ProcessCollector : public IMetricCollector {
	public:
		std::string name() const override;
		bool initialize() override;
		bool collect() override;
		void fill_snapshot(MetricSnapshot& snap) const override;
		void shutdown() override;

		const std::vector<MetricSnapshot::ProcessInfo>& get_all_processes() const { return m_all_processes; }

	private:
		// per process state kept between ticks
		struct PidEntry {
			// process CPU cycles from the last tick
			uint64_t prev_cycles;
			// used to detect PID reuse, if the create time changed the old process exited and a new one was started or similar
			int64_t create_time;
			// set to m_tick_gen on every tick this PID appears in, if not updated got removed from the process list
			uint64_t last_seen;
		};
		std::vector<BYTE> m_buffer;
		std::unordered_map<uint64_t, PidEntry> m_pid_map;

		struct ProcessCandidate {
			uint32_t pid;
			PWSTR name_buffer;
			USHORT name_length;
			float cpu_percent;
			uint64_t ram_bytes;
			uint32_t thread_count;
			uint32_t handle_count;
		};

		std::vector<ProcessCandidate> m_temp_candidates;
		std::vector<uint64_t> m_temp_cycle_delta;

		// cpu percentages are arent useful on the first tick since theres nothing compute deltas against
		bool m_first_tick{ true };

		// incremented every tick to detect which PIDs disappeared since the last collection
		uint64_t m_tick_gen{ 0 };
		uint64_t m_prev_total_cycles{ 0 };
		DWORD m_own_pid{ 0 };
		std::vector<MetricSnapshot::ProcessInfo> m_top_processes;
		std::vector<MetricSnapshot::ProcessInfo> m_all_processes;

		float m_pulsedb_cpu{};
		uint64_t m_pulsedb_ram{};
	};
}
