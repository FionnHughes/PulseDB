#pragma once

#include <windows.h>
#include <pdh.h>
#include <string>
#include <vector>
#include <utility>

namespace pulsedb {
	// thin wrapper around Windows PDH (Performance Data Helper) to handles open/close and counter registration
	class PdhWrapper {
	public:
		PdhWrapper() = default;
		~PdhWrapper();

		bool open();
		void close();

		int add_counter(const std::wstring& counter_path);
		bool collect();

		bool get_double(int counter_idx, double& out) const;
		bool get_all_doubles(int counter_idx, std::vector<std::pair<std::wstring, double>>& out) const;

		bool is_open() const;

		// deleting copy and move functions from the PDH as it should only have the one instance
		PdhWrapper(const PdhWrapper&) = delete;
		PdhWrapper& operator=(const PdhWrapper&) = delete;
		PdhWrapper(PdhWrapper&&) = delete;
		PdhWrapper& operator=(PdhWrapper&&) = delete;
		
	private:
		// the PDH query handle if nullptr means it hasn't been opened yet
		PDH_HQUERY m_query{ nullptr };

		// handle for each counter we've registered, in insertion order
		std::vector<PDH_HCOUNTER> m_counters;

	};
}