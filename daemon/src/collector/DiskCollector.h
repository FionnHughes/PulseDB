#pragma once

#include "IMetricCollector.h"
#include "PdhWrapper.h"
#include "MetricSnapshot.h"

namespace pulsedb {

	class DiskCollector : public IMetricCollector {
	public:
		std::string name() const override;
		bool initialize() override;
		bool collect() override;
		void fill_snapshot(MetricSnapshot& snap) const override;
		void shutdown() override;

	private:
		PdhWrapper m_pdh;

		int m_read_bytes_idx{ -1 };
		int m_write_bytes_idx{ -1 };
		int m_utilization_idx{ -1 };
		int m_queue_depth_idx{ -1 };

		std::vector<MetricSnapshot::DiskStats> m_disks;
		bool m_degraded{ false };
	};
}

