#pragma once

#include <windows.h>

#include "IMetricCollector.h"
#include "MetricSnapshot.h"
#include "NtdllHelper.h"

namespace pulsedb {
	// reads kernel level counters, getting context switches, system calls, page faults, all in per second format
	class SystemMetricsCollector : public IMetricCollector {
	public:
		std::string name() const override;
		bool initialize() override;
		bool collect() override;
		void fill_snapshot(MetricSnapshot& snap) const override;
		void shutdown() override;

	private:
		// uint32_t to match the ULONG fields in the kernel struct 
		uint32_t m_prev_context_switches{ 0 };
		uint32_t m_prev_system_calls{ 0 };
		uint32_t m_prev_page_faults{ 0 };

		uint64_t m_context_switches_per_sec{ 0 };
		uint64_t m_system_calls_per_sec{ 0 };
		uint64_t m_page_faults_per_sec{ 0 };

		// QPC frequency, a constant after the first query, used to convert ticks to seconds
		LARGE_INTEGER m_freq{};

		// QPC tick count from the previous collect() call
		int64_t m_prev_qpc{ 0 };
	};
}
