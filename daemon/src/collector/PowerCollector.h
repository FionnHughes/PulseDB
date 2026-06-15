#pragma once

#include <windows.h>

#include "IMetricCollector.h"

namespace pulsedb {
	class PowerCollector : public IMetricCollector {
	public:
		std::string name() const override;
		bool initialize() override;
		bool collect() override;
		void fill_snapshot(MetricSnapshot& snap) const override;
		void shutdown() override;

	private:
		bool m_on_ac_power{ false };
		bool m_battery_present{ false };
		uint8_t m_battery_percent{ 0 };
		int32_t m_battery_seconds_remaining{ -1 };
		int32_t m_battery_full_seconds_remaining{ -1 };
	};
}
