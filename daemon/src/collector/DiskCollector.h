#pragma once

#include "IMetricCollector.h"
#include "PdhWrapper.h"
#include "MetricSnapshot.h"

namespace pulsedb {

	// uses PDH (Windows Performance Data Helper) to get per disk read/write bytes and utilization
	class DiskCollector : public IMetricCollector {
	public:
		std::string name() const override;
		bool initialize() override;
		bool collect() override;
		void fill_snapshot(MetricSnapshot& snap) const override;
		void shutdown() override;

	private:
		PdhWrapper m_pdh;
		// index into PdhWrapper's counter array, returned by add_counter() - -1 means not registered
		int m_read_bytes_idx{ -1 };
		int m_write_bytes_idx{ -1 };
		int m_utilization_idx{ -1 };
		int m_queue_depth_idx{ -1 };

		std::vector<MetricSnapshot::DiskStats> m_disks;

		// PDH failed to set up so we would skip collection
		bool m_degraded{ false };
	};
}

