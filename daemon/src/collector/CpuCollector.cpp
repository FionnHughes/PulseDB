
#include <cstdint>
#include <windows.h>
#include <winternl.h>

#include "CpuCollector.h"
#include "MetricSnapshot.h"
#include "NtdllHelper.h"

namespace {
	uint64_t filetime_to_uint64(const FILETIME& ft) {
		return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	}
}
namespace pulsedb {
	std::string CpuCollector::name() const {
		return "cpu";
	}

	bool CpuCollector::initialize() {
		SYSTEM_INFO info{};
		GetSystemInfo(&info);

		m_core_count = info.dwNumberOfProcessors;
		if (m_core_count <= 0) {
			m_degraded = true;
			return false;
		}

		m_prev_idle_ticks.resize(m_core_count);
		m_prev_kernel_ticks.resize(m_core_count);
		m_prev_user_ticks.resize(m_core_count);

		m_per_core_percent.resize(m_core_count);
		m_per_core_freq_mhz.resize(m_core_count);

		m_prev_cycle_ticks.resize(m_core_count);

		if (!NtdllHelper::get().is_loaded()) {
			m_degraded = true;
			return false;
		}

		GetSystemTimes(&m_prev_idle, &m_prev_kernel, &m_prev_user);

		std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> buffer(m_core_count);

		auto fn = NtdllHelper::get().query_fn();
		
		ULONG bytes_returned = 0;
		NTSTATUS status = fn(
			SystemProcessorPerformanceInformation, 
			buffer.data(), 
			static_cast<ULONG>(sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * m_core_count), 
			&bytes_returned
		);

		if (status < 0) {
			m_degraded = true;
			return false;
		}

		for (int i = 0; i < m_core_count; ++i) {
			m_prev_idle_ticks[i] = static_cast<uint64_t>(buffer[i].IdleTime.QuadPart);
			m_prev_kernel_ticks[i] = static_cast<uint64_t>(buffer[i].KernelTime.QuadPart);
			m_prev_user_ticks[i] = static_cast<uint64_t>(buffer[i].UserTime.QuadPart);
		}

		return true;
	}

	bool CpuCollector::collect() {
		if (m_degraded) {
			return false;
		}

		FILETIME idle_current_local{};
		FILETIME kernel_current_local{};
		FILETIME user_current_local{};

		if (!GetSystemTimes(&idle_current_local, &kernel_current_local, &user_current_local)) {
			return false;
		}

		uint64_t idle_now = filetime_to_uint64(idle_current_local);
		uint64_t kernel_now = filetime_to_uint64(kernel_current_local);
		uint64_t user_now = filetime_to_uint64(user_current_local);

		uint64_t idle_prev = filetime_to_uint64(m_prev_idle);
		uint64_t kernel_prev = filetime_to_uint64(m_prev_kernel);
		uint64_t user_prev = filetime_to_uint64(m_prev_user);

		uint64_t idle_delta = idle_now - idle_prev;
		uint64_t kernel_delta = kernel_now - kernel_prev;
		uint64_t user_delta = user_now - user_prev;

		uint64_t total = kernel_delta + user_delta;
		uint64_t busy = (idle_delta < total) ? (total - idle_delta) : 0;

		if (total == 0) {
			m_cpu_total_percent = 0.0f;
		}
		else {
			m_cpu_total_percent = static_cast<float>(busy) / total * 100.0f;
		}
		
		m_prev_idle = idle_current_local;
		m_prev_kernel = kernel_current_local;
		m_prev_user = user_current_local;

		std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> buffer(m_core_count);

		auto fn = NtdllHelper::get().query_fn();

		ULONG bytes_returned = 0;
		NTSTATUS status = fn(
			SystemProcessorPerformanceInformation,
			buffer.data(),
			static_cast<ULONG>(sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * m_core_count),
			&bytes_returned
		);

		if (status < 0) {
			m_degraded = true;
			return false;
		}

		for (int i = 0; i < m_core_count; ++i) {
			uint64_t core_idle_now = static_cast<uint64_t>(buffer[i].IdleTime.QuadPart);
			uint64_t core_kernel_now = static_cast<uint64_t>(buffer[i].KernelTime.QuadPart);
			uint64_t core_user_now = static_cast<uint64_t>(buffer[i].UserTime.QuadPart);

			uint64_t core_idle_delta = core_idle_now - m_prev_idle_ticks[i];
			uint64_t core_kernel_delta = core_kernel_now - m_prev_kernel_ticks[i];
			uint64_t core_user_delta = core_user_now - m_prev_user_ticks[i];

			uint64_t core_total = core_kernel_delta + core_user_delta;
			uint64_t core_busy = (core_idle_delta < core_total) ? (core_total - core_idle_delta) : 0;

			if (core_total == 0) {
				m_per_core_percent[i] = 0.0f;
			}
			else {
				m_per_core_percent[i] = static_cast<float>(core_busy) / static_cast<float>(core_total) * 100.0f;
			}

			m_prev_idle_ticks[i] = core_idle_now;
			m_prev_kernel_ticks[i] = core_kernel_now;
			m_prev_user_ticks[i] = core_user_now;
		}
		return true;
	}

	void CpuCollector::fill_snapshot(MetricSnapshot& snap) const {
		snap.cpu_total_percent = m_cpu_total_percent;
		snap.cpu_per_core_percent = m_per_core_percent;
		snap.cpu_per_core_frequency_mhz = m_per_core_freq_mhz;
	}

	void CpuCollector::shutdown() {}
}