#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>

#include "NetworkCollector.h"
#include "common/Utils.h"

namespace pulsedb {
	// returns name just in case i forget this obscure function
	std::string NetworkCollector::name() const {
		return "network";
	}

	bool NetworkCollector::initialize() {
		LARGE_INTEGER freq;

		if (!QueryPerformanceFrequency(&freq)) {
			m_degraded = true;
			return false;
		}
		m_freq = freq.QuadPart;

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

	// gets the current adapter table, computes deltas against the previous tick values, divides by elapsed time for per-second rates
	bool NetworkCollector::collect() {
		if (m_degraded) {
			return false;
		}
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		double elapsed_secs = static_cast<double>(now.QuadPart - m_prev_ticks) / m_freq;

		if (elapsed_secs <= 0) {
			m_prev_ticks = now.QuadPart;
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
			// guard against counter resets (like an adapter off and on), treat as 0 delta rather than a huge spike
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

			// dividing by actual elapsed time so im not assuming exactly 1s
			uint64_t bytes_in_per_sec = static_cast<uint64_t>(in_bytes_delta / elapsed_secs);
			uint64_t bytes_out_per_sec = static_cast<uint64_t>(out_bytes_delta / elapsed_secs);
			uint64_t packets_in_per_sec = static_cast<uint64_t>(in_packets_delta / elapsed_secs);
			uint64_t packets_out_per_sec = static_cast<uint64_t>(out_packets_delta / elapsed_secs);

			MetricSnapshot::NetworkStats entry{
				to_utf8(row.Alias, static_cast<int>(wcsnlen(row.Alias, IF_MAX_STRING_SIZE))),
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