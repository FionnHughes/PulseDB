#include <algorithm>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#define NOMINMAX
#include <windows.h>
#include <winternl.h>

#include "ProcessCollector.h"
#include "common/Utils.h"

namespace {
	//this is the full SYSTEM_PROCESS_INFORMATION for 64 bit Win
	//winternl.h only shows a few fields, minimal amount, so i have it defined here to access CycleTime, WorkingSetSize, HandleCount etc.
	//order and offsets from geoffchappell.com docs.
	struct FULL_SYSTEM_PROCESS_INFORMATION {
		ULONG          NextEntryOffset;
		ULONG          NumberOfThreads;
		LARGE_INTEGER  WorkingSetPrivateSize;
		ULONG          HardFaultCount;
		ULONG          NumberOfThreadsHighWatermark;
		ULONGLONG      CycleTime;
		LARGE_INTEGER  CreateTime;
		LARGE_INTEGER  UserTime;
		LARGE_INTEGER  KernelTime;
		UNICODE_STRING ImageName;
		LONG           BasePriority;
		PVOID          UniqueProcessId;
		PVOID          InheritedFromUniqueProcessId;
		ULONG          HandleCount;
		ULONG          SessionId;
		ULONG_PTR      UniqueProcessKey;
		SIZE_T         PeakVirtualSize;
		SIZE_T         VirtualSize;
		ULONG          PageFaultCount;
		SIZE_T         PeakWorkingSetSize;
		SIZE_T         WorkingSetSize;
	};

	//static_asserts to confirm layout correctness as it must match winternal actual struct layout
	static_assert(offsetof(FULL_SYSTEM_PROCESS_INFORMATION, CycleTime) == 0x18);
	static_assert(offsetof(FULL_SYSTEM_PROCESS_INFORMATION, ImageName) == 0x38);
	static_assert(offsetof(FULL_SYSTEM_PROCESS_INFORMATION, UniqueProcessId) == 0x50);
	static_assert(offsetof(FULL_SYSTEM_PROCESS_INFORMATION, HandleCount) == 0x60);
	static_assert(offsetof(FULL_SYSTEM_PROCESS_INFORMATION, WorkingSetSize) == 0x90);
}

namespace pulsedb {
	std::string ProcessCollector::name() const {
		return "processes";
	}

	// initializing the helper and sizing vectors to pre allocate space before running the program
	bool ProcessCollector::initialize() {
		if (!NtdllHelper::get().is_loaded()) {
			return false;
		}
		m_own_pid = GetCurrentProcessId();
		m_buffer.resize(512 * 1024);
		m_top_processes.reserve(25);
		m_pid_map.reserve(512);
		m_temp_cycle_delta.reserve(256);

		return true;
	}

	// queries System process info using the helper
	bool ProcessCollector::collect() {
		m_temp_candidates.clear();
		m_temp_cycle_delta.clear();
		m_all_processes.clear();

		m_tick_gen++;
		auto fn = pulsedb::NtdllHelper::get().query_fn();

		ULONG bytes_returned = 0;

		while (true) {
			NTSTATUS status = fn(
				SystemProcessInformation,
				m_buffer.data(),
				m_buffer.size(),
				&bytes_returned
			);
			//if success yay
			if (status == STATUS_SUCCESS) {
				break;
			}
			//buffer might be too small and if the function returns the actual bytes needed then resize
			if (status == STATUS_INFO_LENGTH_MISMATCH) {
				if (bytes_returned != 0) {
					m_buffer.resize(bytes_returned);
				}
				else {
					m_buffer.resize(m_buffer.size() * 2);
				}
				continue;
			}
			return false;
		}

		//setting a cursor to move through the data and read entries as SYSTEM_PROCESS_INFORMATION
		BYTE* cursor = m_buffer.data();
		uint64_t total_cycles = 0;

		while (true) {
			FULL_SYSTEM_PROCESS_INFORMATION* entry = reinterpret_cast<FULL_SYSTEM_PROCESS_INFORMATION*>(cursor);
			uint64_t pid = reinterpret_cast<uint64_t>(entry->UniqueProcessId);

			total_cycles += entry->CycleTime;

			// pid 0 is the System Idle Process so i count its cycles toward the total but don't add it to the process list
			if (pid == 0) {
				if (entry->NextEntryOffset == 0) break;
				cursor += entry->NextEntryOffset;
				continue;
			}	

			auto it = m_pid_map.find(pid);
			if (it != m_pid_map.end() && it->second.create_time == entry->CreateTime.QuadPart) {
				m_temp_cycle_delta.push_back(entry->CycleTime - it->second.prev_cycles);
				it->second.prev_cycles = entry->CycleTime;
				it->second.last_seen = m_tick_gen;
			}
			else {
				m_temp_cycle_delta.push_back(0);
				m_pid_map.insert_or_assign(pid, PidEntry{ entry->CycleTime, entry->CreateTime.QuadPart, m_tick_gen });
			}

			ProcessCandidate cand{
				static_cast<uint32_t>(pid),
				entry->ImageName.Buffer,
				entry->ImageName.Length,
				0.0f,
				static_cast<uint64_t>(entry->WorkingSetSize),
				static_cast<uint32_t>(entry->NumberOfThreads),
				static_cast<uint32_t>(entry->HandleCount)
			};
			m_temp_candidates.push_back(cand);

			if (entry->NextEntryOffset == 0) break;
			cursor += entry->NextEntryOffset;

		}
		// remove any pids that weren't seen this tick as those processes exited
		std::erase_if(m_pid_map, [&](const auto& kv) {
			return kv.second.last_seen != m_tick_gen;
		});
		uint64_t total_delta = total_cycles - m_prev_total_cycles;

		for (size_t i = 0; i < m_temp_candidates.size(); i++) {
			if (!m_first_tick && total_delta != 0) {
				m_temp_candidates[i].cpu_percent = static_cast<float>(
					static_cast<double>(m_temp_cycle_delta[i]) / static_cast<double>(total_delta) * 100.0);
			}
			else {
				m_temp_candidates[i].cpu_percent = 0.0f;
			}
			if (m_temp_candidates[i].pid == m_own_pid) {
				m_pulsedb_cpu = m_temp_candidates[i].cpu_percent;
				m_pulsedb_ram = m_temp_candidates[i].ram_bytes;
			} 
		}
		// partial sort is faster than full sort since we only need the top 25
		std::partial_sort(
			m_temp_candidates.begin(),
			m_temp_candidates.begin() + std::min((size_t)25, m_temp_candidates.size()),
			m_temp_candidates.end(),
			[](const ProcessCandidate& a, const ProcessCandidate& b) { return a.cpu_percent > b.cpu_percent; }
		);
		m_top_processes.clear();
		size_t top_n = std::min((size_t)25, m_temp_candidates.size());
		for (size_t i = 0; i < m_temp_candidates.size(); i++) {
			auto& c = m_temp_candidates[i];
			std::string name = c.name_buffer ? to_utf8(c.name_buffer, c.name_length / 2) : "System";
			MetricSnapshot::ProcessInfo info{
				c.pid, name, c.cpu_percent, c.ram_bytes,
				c.thread_count, c.handle_count, 0, 0
			};
			if (i < top_n) {
				m_top_processes.push_back(info);
			}
			m_all_processes.push_back(std::move(info));
		}
		m_prev_total_cycles = total_cycles;
		// after the first tick the deltas are valid
		m_first_tick = false;
		return true;
	}

	void ProcessCollector::fill_snapshot(MetricSnapshot& snap) const {
		snap.top_processes = m_top_processes;
		snap.pulsedb_pid = m_own_pid;
		snap.pulsedb_cpu_percent = m_pulsedb_cpu;
		snap.pulsedb_ram_bytes = m_pulsedb_ram;
	}
	void ProcessCollector::shutdown() {
		m_pid_map.clear();
		m_top_processes.clear();
		m_all_processes.clear();
		m_temp_cycle_delta.clear();
		m_prev_total_cycles = 0;
	}

}