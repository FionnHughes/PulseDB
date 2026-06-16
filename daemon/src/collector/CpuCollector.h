#pragma once

#include <cstdint>
#include <vector>
#include <windows.h>
#include <winternl.h>

#include "IMetricCollector.h"

namespace pulsedb{

	// measures total and per core CPU usage using Windows system timer counters from the kernel
	class CpuCollector : public IMetricCollector {
	public:
		std::string name() const override;
		bool initialize() override;
		bool collect() override;
		void fill_snapshot(MetricSnapshot& snap) const override;
		void shutdown() override;

	private:
		int m_core_count{ 0 };

		// previous tick values for total CPU calculation, delta between now and prev gives usage percentage
		FILETIME m_prev_idle{};
		FILETIME m_prev_kernel{};
		FILETIME m_prev_user{};


		float m_cpu_total_percent{ 0.0f };
		
		std::vector<uint64_t> m_prev_idle_ticks;
		std::vector<uint64_t> m_prev_kernel_ticks;
		std::vector<uint64_t> m_prev_user_ticks;

		std::vector<float> m_per_core_percent;
		std::vector<float> m_per_core_freq_mhz;

		std::vector<uint64_t> m_prev_cycle_ticks;

		// NtQuerySystemInformation fills this with per core timing data
		std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> m_perf_buffer;

		// set to true if init or collection fails, if collect() returns false and then stop trying
		bool m_degraded{ false };

	};
}