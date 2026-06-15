#include <string>

#include "DiskCollector.h"

namespace {
	//storing strange path names in variables to help readability 
	constexpr const wchar_t* read_bytes_path = L"\\PhysicalDisk(*)\\Disk Read Bytes/sec";
	constexpr const wchar_t* read_write_path = L"\\PhysicalDisk(*)\\Disk Write Bytes/sec";
	constexpr const wchar_t* time_path = L"\\PhysicalDisk(*)\\% Disk Time";
	constexpr const wchar_t* queue_path = L"\\PhysicalDisk(*)\\Current Disk Queue Length";
	
	//need to convert wstring to utf8 to store names in my metric statistics struct
	std::string to_utf8(const std::wstring& w_string) {
		//getting size which the wide string will return
		int size = WideCharToMultiByte(CP_UTF8, 0, w_string.c_str(), static_cast<int>(w_string.size()), NULL, 0, NULL, NULL);
		if (!size) {
			return std::string();
		}
		std::string utf8(size, '\0');

		WideCharToMultiByte(CP_UTF8, 0, w_string.c_str(), static_cast<int>(w_string.size()), utf8.data(), size, NULL, NULL);

		return utf8;
	}
}

namespace pulsedb {
	//name! mind blowing
	std::string DiskCollector::name() const {
		return "disk";
	}


	//intilializing collector
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

		//initial collection
		m_pdh.collect();
		return true;
	}

	//proper collection to reference against
	bool DiskCollector::collect() {
		if (m_degraded) {
			return false;
		}

		if (!m_pdh.collect()) {
			return false;
		}
		//defining a vector of pairs to get return values from counter array
		std::vector<std::pair<std::wstring, double>> reads, writes, utilization, queue;

		if (!m_pdh.get_all_doubles(m_read_bytes_idx, reads)) { return false; }
		if (!m_pdh.get_all_doubles(m_write_bytes_idx, writes)) { return false; }
		if (!m_pdh.get_all_doubles(m_utilization_idx, utilization)) { return false; }
		if (!m_pdh.get_all_doubles(m_queue_depth_idx, queue)) { return false; }

		m_disks.clear();

		//assuming here that the returns line up with one another index wise
		for (size_t i = 0; i < reads.size(); i++) {
			if (reads[i].first == L"_Total") {
				continue;
			}
			MetricSnapshot::DiskStats entry;
			entry.device_name = to_utf8(reads[i].first);
			entry.read_bytes_per_sec = static_cast<uint64_t>(reads[i].second);
			entry.write_bytes_per_sec = static_cast<uint64_t>(writes[i].second);
			entry.utilization_percent = static_cast<float>(utilization[i].second);
			entry.queue_depth = static_cast<uint64_t>(queue[i].second);

			m_disks.push_back(entry);
		}
		return true;
	}

	//filling snapshot excatly from earlier snapshot
	void DiskCollector::fill_snapshot(MetricSnapshot& snap) const {
		snap.disks = m_disks;
	}

	//making sure everything closes
	void DiskCollector::shutdown() {
		m_pdh.close();
	}
}