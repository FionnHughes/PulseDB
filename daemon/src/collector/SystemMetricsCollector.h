#pragma once

#include <windows.h>

#include "IMetricCollector.h"
#include "MetricSnapshot.h"
#include "NtdllHelper.h"

namespace pulsedb {
	class SystemMetricsCollector : public IMetricCollector {
	public:
		std::string name() const override;
		bool initialize() override;
		bool collect() override;
		void fill_snapshot(MetricSnapshot& snap) const override;
		void shutdown() override;

	private:
		uint64_t m_prev_context_switches{ 0 };
		uint64_t m_prev_system_calls{ 0 };
		uint64_t m_prev_page_faults{ 0 };

		uint64_t m_context_switches_per_sec{ 0 };
		uint64_t m_system_calls_per_sec{ 0 };
		uint64_t m_page_faults_per_sec{ 0 };
	};
}
