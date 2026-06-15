#pragma once

#include <windows.h>
#include <pdh.h>
#include <string>
#include <vector>
#include <utility>

namespace pulsedb {

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

		PdhWrapper(const PdhWrapper&) = delete;
		PdhWrapper& operator=(const PdhWrapper&) = delete;
		PdhWrapper(PdhWrapper&&) = delete;
		PdhWrapper& operator=(PdhWrapper&&) = delete;
		
	private:
		PDH_HQUERY m_query{ nullptr };
		std::vector<PDH_HCOUNTER> m_counters;

	};
}