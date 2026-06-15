#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>

#include "NetworkCollector.h"

namespace {
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
	//name just in case i forget this obscure function
	std::string NetworkCollector::name() const {
		return "network";
	}

	bool NetworkCollector::initialize() {
		LARGE_INTEGER freq;

		//querying and storing freq
		if (!QueryPerformanceFrequency(&freq)) {
			m_degraded = true;
			return false;
		}
		m_freq = freq.QuadPart;

		//creating tables
		MIB_IF_TABLE2* table = nullptr;

		if (GetIfTable2(&table) != NO_ERROR) {
			m_degraded = true;
			return false;
		}
		
		//storing ticks now to compare against later
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		m_prev_ticks = now.QuadPart;
		
		//looping through the tables rows and extracting info
		for (ULONG i = 0; i < table->NumEntries; i++) {
			const auto& row = table->Table[i];
			if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL) {
				continue;
			}
			PrevCounters counter {
				row.InOctets,
				row.OutOctets,
				row.InUcastPkts + row.InNUcastPkts,
				row.OutUcastPkts + row.OutNUcastPkts
			};

			m_prev.emplace(row.InterfaceLuid.Value, counter);
			
		}
		FreeMibTable(table);
		return true;
	}

	bool NetworkCollector::collect() {
		if (m_degraded) {
			return false;
		}
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		double elapsed_secs = static_cast<double>(now.QuadPart - m_prev_ticks) / m_freq;

		if (elapsed_secs <= 0) {
			return false;
		}

		MIB_IF_TABLE2* table = nullptr;

		if (GetIfTable2(&table) != NO_ERROR) {
			return false;
		}
		m_adapters.clear();

		for (ULONG i = 0; i < table->NumEntries; i++) {
			const auto& row = table->Table[i];
			if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL) {
				continue;
			}
			uint64_t luid = row.InterfaceLuid.Value;
			auto it = m_prev.find(luid);
			if (it == m_prev.end()) {
				// not found, insert only current values no comparison
				m_prev[luid] = PrevCounters{
					row.InOctets,
					row.OutOctets,
					row.InUcastPkts + row.InNUcastPkts,
					row.OutUcastPkts + row.OutNUcastPkts
				};
				continue;
			}
			uint64_t in_bytes_delta = (row.InOctets > it->second.in_octets)
				? (row.InOctets - it->second.in_octets)
				: 0;
			uint64_t out_bytes_delta = (row.OutOctets > it->second.out_octets)
				? (row.OutOctets - it->second.out_octets)
				: 0;
			uint64_t in_packets_delta = ((row.InUcastPkts + row.InNUcastPkts) > it->second.in_packets)
				? ((row.InUcastPkts + row.InNUcastPkts) - it->second.in_packets)
				: 0;
			uint64_t out_packets_delta = ((row.OutUcastPkts + row.OutNUcastPkts) > it->second.out_packets)
				? ((row.OutUcastPkts + row.OutNUcastPkts) - it->second.out_packets)
				: 0;

			uint64_t bytes_in_per_sec = static_cast<uint64_t>(in_bytes_delta / elapsed_secs);
			uint64_t bytes_out_per_sec = static_cast<uint64_t>(out_bytes_delta / elapsed_secs);
			uint64_t packets_in_per_sec = static_cast<uint64_t>(in_packets_delta / elapsed_secs);
			uint64_t packets_out_per_sec = static_cast<uint64_t>(out_packets_delta / elapsed_secs);

			MetricSnapshot::NetworkStats entry{
				to_utf8(std::wstring(row.Alias)),
				bytes_in_per_sec,
				bytes_out_per_sec,
				packets_in_per_sec,
				packets_out_per_sec
			};

			m_adapters.push_back(entry);

			it->second = PrevCounters{
					row.InOctets,
					row.OutOctets,
					row.InUcastPkts + row.InNUcastPkts,
					row.OutUcastPkts + row.OutNUcastPkts
			};
		}
		m_prev_ticks = now.QuadPart;
		FreeMibTable(table);
		return true;
	}

	void NetworkCollector::fill_snapshot(MetricSnapshot& snap) const {
		snap.network_adapters = m_adapters;
	}

	void NetworkCollector::shutdown() {}
}