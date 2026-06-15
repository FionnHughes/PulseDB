#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>

#include "IMetricCollector.h"
#include "MetricSnapshot.h"

namespace pulsedb {
	class NetworkCollector : public IMetricCollector {
	public:
		std::string name() const override;
		bool initialize() override;
		bool collect() override;
		void fill_snapshot(MetricSnapshot& snap) const override;
		void shutdown() override;

	private:
		//unique counters for wifi testing
		struct PrevCounters {
			uint64_t in_octets{};
			uint64_t out_octets{};
			uint64_t in_packets{};
			uint64_t out_packets{};
		};

		std::unordered_map<uint64_t, PrevCounters> m_prev;

		//query performance counter and freq
		int64_t m_prev_ticks{ 0 };
		int64_t m_freq{ 0 };

		std::vector<MetricSnapshot::NetworkStats> m_adapters;
		bool m_degraded{ false };
	};
}
