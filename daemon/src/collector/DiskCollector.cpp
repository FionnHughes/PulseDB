#include <string>

#include "DiskCollector.h"
#include "common/Utils.h"

namespace {
	//storing strange path names in variables to help readability 
	constexpr const wchar_t* read_bytes_path = L"\\PhysicalDisk(*)\\Disk Read Bytes/sec";
	constexpr const wchar_t* read_write_path = L"\\PhysicalDisk(*)\\Disk Write Bytes/sec";
	constexpr const wchar_t* time_path = L"\\PhysicalDisk(*)\\% Disk Time";
	constexpr const wchar_t* queue_path = L"\\PhysicalDisk(*)\\Current Disk Queue Length";
}

namespace pulsedb {
	//name! mind blowing
	std::string DiskCollector::name() const {
		return "disk";
	}

	// opens a PDH query and registers the four per disk counters we want
	bool DiskCollector::initialize() {
		//opens a new pdh 
		if (!m_pdh.open()) {
			m_degraded = true;
			return false;
		}

		//registering counters and getting idxs
		m_read_bytes_idx = m_pdh.add_counter(read_bytes_path);
		if (m_read_bytes_idx < 0) {
			m_degraded = true;
			return false;
		}
		m_write_bytes_idx = m_pdh.add_counter(read_write_path);
		if (m_write_bytes_idx < 0) {
			m_degraded = true;
			return false;
		}
		m_utilization_idx = m_pdh.add_counter(time_path);
		if (m_utilization_idx < 0) {
			m_degraded = true;
			return false;
		}
		m_queue_depth_idx = m_pdh.add_counter(queue_path);
		if (m_queue_depth_idx < 0) {
			m_degraded = true;
			return false;
		}

		m_pdh.collect();
		return true;
	}

	// collects the PDH query and splits the results into per disk stats
	bool DiskCollector::collect() {
		if (m_degraded) {
			return false;
		}

		if (!m_pdh.collect()) {
			return false;
		}
		// defining a vector of pairs to get return values from counter array
		std::vector<std::pair<std::wstring, double>> reads, writes, utilization, queue;

		if (!m_pdh.get_all_doubles(m_read_bytes_idx, reads)) { return false; }
		if (!m_pdh.get_all_doubles(m_write_bytes_idx, writes)) { return false; }
		if (!m_pdh.get_all_doubles(m_utilization_idx, utilization)) { return false; }
		if (!m_pdh.get_all_doubles(m_queue_depth_idx, queue)) { return false; }

		m_disks.clear();

		// assuming here that the returns line up with one another index wise
		for (size_t i = 0; i < reads.size(); i++) {
			// PDH returns an aggregate "_Total" entry for each counter but we skip it, we want per disk
			if (reads[i].first == L"_Total") {
				continue;
			}
			MetricSnapshot::DiskStats entry;
			entry.device_name = to_utf8(reads[i].first.c_str(), static_cast<int>(reads[i].first.size()));
			entry.read_bytes_per_sec = static_cast<uint64_t>(reads[i].second);
			entry.write_bytes_per_sec = static_cast<uint64_t>(writes[i].second);
			entry.utilization_percent = static_cast<float>(utilization[i].second);
			entry.queue_depth = static_cast<uint64_t>(queue[i].second);

			m_disks.push_back(entry);
		}
		return true;
	}

	void DiskCollector::fill_snapshot(MetricSnapshot& snap) const {
		snap.disks = m_disks;
	}

	void DiskCollector::shutdown() {
		m_pdh.close();
	}
}