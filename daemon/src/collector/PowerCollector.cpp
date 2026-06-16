#include <windows.h>

#include "MetricSnapshot.h"
#include "PowerCollector.h"

namespace pulsedb {
	//still returning name 
	std::string PowerCollector::name() const {
		return "power";
	}

	bool PowerCollector::initialize() {
		return true;
	}

	// calls GetSystemPowerStatus and copies all fields we want out
	bool PowerCollector::collect() {
		SYSTEM_POWER_STATUS status;
		if (!GetSystemPowerStatus(&status)) {
			return false;
		}
		// ACLineStatus: 0=battery, 1=AC, 255=unknown and we are treating unknown as "not on AC"
		m_on_ac_power = (status.ACLineStatus == 1);
		// BatteryFlag 128 means no battery present and every other value means one exists
		m_battery_present = (status.BatteryFlag != 128);
		m_battery_percent = status.BatteryLifePercent;
		// 0xFFFFFFFF is when windows is saying it doesn't know how much battery is left
		m_battery_seconds_remaining = (status.BatteryLifeTime == 0xFFFFFFFF) ?  -1 : static_cast<int32_t>(status.BatteryLifeTime);
		m_battery_full_seconds_remaining = (status.BatteryFullLifeTime == 0xFFFFFFFF) ? -1 : static_cast<int32_t>(status.BatteryFullLifeTime);

		return true;
	}

	void PowerCollector::fill_snapshot(MetricSnapshot& snap) const {
		snap.power.on_ac_power = m_on_ac_power;
		snap.power.battery_present = m_battery_present;
		snap.power.battery_percent = m_battery_percent;
		snap.power.battery_seconds_remaining = m_battery_seconds_remaining;
		snap.power.battery_full_seconds_remaining = m_battery_full_seconds_remaining;

	}
	void PowerCollector::shutdown() {
		m_on_ac_power = false;
		m_battery_present = false;
		m_battery_percent = 0;
		m_battery_seconds_remaining = -1;
		m_battery_full_seconds_remaining = -1;
	}
}