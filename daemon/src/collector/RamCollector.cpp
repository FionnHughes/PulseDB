#include <windows.h>

#include "RamCollector.h"
#include "MetricSnapshot.h"

namespace pulsedb {
	std::string RamCollector::name() const {
		return "ram";
	}

	bool RamCollector::initialize() {
		return true;
	}

	bool RamCollector::collect() {
		MEMORYSTATUSEX status{};
		status.dwLength = sizeof(MEMORYSTATUSEX);

		if (!GlobalMemoryStatusEx(&status)) {
			return false;
		}

		m_total = status.ullTotalPhys;
		m_available = status.ullAvailPhys;
		m_used = m_total - m_available;
		m_swap_total = (status.ullTotalPageFile > status.ullTotalPhys)
			? (status.ullTotalPageFile - status.ullTotalPhys)
			: 0;
		uint64_t pagefile_used = status.ullTotalPageFile - status.ullAvailPageFile;
		uint64_t phys_used = status.ullTotalPhys - status.ullAvailPhys;
		m_swap_used = (pagefile_used > phys_used) ? (pagefile_used - phys_used) : 0;
		return true;
	}

	void RamCollector::fill_snapshot(MetricSnapshot& snap) const {
		snap.ram_total_bytes = m_total;
		snap.ram_available_bytes = m_available;
		snap.ram_used_bytes = m_used;
		snap.swap_total_bytes = m_swap_total;
		snap.swap_used_bytes = m_swap_used;
	}

	void RamCollector::shutdown() {}
}