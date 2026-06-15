#pragma once

#include <vector>
#include <unordered_map>
#include <windows.h>

#include "IMetricCollector.h"
#include "MetricSnapshot.h"
#include "NtdllHelper.h"

namespace pulsedb {
	class ProcessCollector : public IMetricCollector {
	public:
		std::string name() const override;
		bool initialize() override;
		bool collect() override;
		void fill_snapshot(MetricSnapshot& snap) const override;
		void shutdown() override;

	private:
		struct PidEntry {
			uint64_t prev_cycles;
			int64_t create_time;
			uint64_t last_seen;
		};
		std::vector<BYTE> m_buffer;
		std::unordered_map<uint64_t, PidEntry> m_pid_map;

		std::vector<MetricSnapshot::ProcessInfo> m_temp_processes;
		std::vector<uint64_t> m_temp_cycle_delta;

		uint64_t m_tick_gen{ 0 };
		uint64_t m_prev_total_cycles{ 0 };
		DWORD m_own_pid{ 0 };
		std::vector<MetricSnapshot::ProcessInfo> m_top_processes;

		DWORD m_pulsedb_pid{};
		float m_pulsedb_cpu{};
		uint64_t m_pulsedb_ram{};
	};
}
