#pragma once

#include <cstdint>

#include "IMetricCollector.h"

namespace pulsedb {

	class RamCollector : public IMetricCollector {
	public:
		std::string name() const override;
		bool initialize() override;
		bool collect() override;
		void fill_snapshot(MetricSnapshot& snap) const override;
		void shutdown() override;

	private:
		uint64_t m_total{ 0 };
		uint64_t m_used{ 0 };
		uint64_t m_available{ 0 };
		uint64_t m_swap_total{ 0 };
		uint64_t m_swap_used{ 0 };
	};
}

