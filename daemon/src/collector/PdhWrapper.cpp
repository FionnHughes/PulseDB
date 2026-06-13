#include "PdhWrapper.h"
#include <pdhmsg.h>

namespace pulsedb {
	PdhWrapper::~PdhWrapper() {
		close();
	}

	bool PdhWrapper::open() {
		if (PdhOpenQueryW(nullptr, 0, &m_query) != ERROR_SUCCESS) {
			m_query = nullptr;
			return false;
		}
		return true;
	}

	void PdhWrapper::close() {
		if (m_query == nullptr) {
			return;
		}
		PdhCloseQuery(m_query);
		m_query = nullptr;
		m_counters.clear();
	}

	int PdhWrapper::add_counter(const std::wstring& counter_path) {
		if (!is_open()) {
			return -1;
		}
		PDH_HCOUNTER counter;

		if (PdhAddCounterW(m_query, counter_path.c_str(), 0, &counter) != ERROR_SUCCESS) {
			return -1;
		}
		m_counters.push_back(counter);

		return static_cast<int>(m_counters.size() - 1);
	}

	bool PdhWrapper::collect() {
		if (!is_open()) {
			return false;
		}
		return PdhCollectQueryData(m_query) == ERROR_SUCCESS;
	}

	bool PdhWrapper::get_double(int counter_idx, double& out) const {
		if (counter_idx < 0 || counter_idx >= static_cast<int>(m_counters.size())) {
			return false;
		}

		PDH_FMT_COUNTERVALUE value;

		if (PdhGetFormattedCounterValue(m_counters[counter_idx], PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS) {
			return false;
		}
		out = value.doubleValue;

		return true;
	}

	bool PdhWrapper::get_all_doubles(int counter_idx, std::vector<std::pair<std::wstring, double>>& out) const {
		if (counter_idx < 0 || counter_idx >= static_cast<int>(m_counters.size())) {
			return false;
		}

		DWORD buffer_size = 0, item_count = 0;

		if (PdhGetFormattedCounterArrayW(m_counters[counter_idx], PDH_FMT_DOUBLE, &buffer_size, &item_count, nullptr) != PDH_MORE_DATA) {
			return false;
		}
		std::vector<BYTE> raw_buffer(buffer_size);
		auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(raw_buffer.data());

		if (PdhGetFormattedCounterArrayW(m_counters[counter_idx], PDH_FMT_DOUBLE, &buffer_size, &item_count, items) != ERROR_SUCCESS) {
			return false;
		}
		out.clear();

		for (DWORD i = 0; i < item_count; i++) {
			out.emplace_back(std::wstring(items[i].szName), items[i].FmtValue.doubleValue);
		}

		return true;
	}

	bool PdhWrapper::is_open() const {
		return m_query != nullptr;
	}
}