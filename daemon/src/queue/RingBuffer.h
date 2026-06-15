#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>
#include <algorithm>

template<typename T, size_t Capacity>
class RingBuffer {
public:
	void push(const T& item) {
		size_t current_write_idx = m_write_idx.fetch_add(1, std::memory_order_release);
		m_storage[current_write_idx % Capacity] = item;
	}

	void push(T&& item) {
		size_t current_write_idx = m_write_idx.fetch_add(1, std::memory_order_release);
		m_storage[current_write_idx % Capacity] = std::move(item);
	}

	std::optional<T> latest() {
		size_t write_idx = m_write_idx.load(std::memory_order_relaxed);
		if (write_idx == 0) {
			return std::nullopt;
		}
		return m_storage[(write_idx - 1) % Capacity];
	}

	size_t size() const {
		return std::min(m_write_idx.load(std::memory_order_relaxed), Capacity);
	}

	T get(size_t index) {
		size_t write_idx = m_write_idx.load(std::memory_order_relaxed);
		return m_storage[(write_idx - 1 - index) % Capacity];
	}

private:
	std::array<T, Capacity> m_storage;
	alignas(64) std::atomic<size_t> m_write_idx{ 0 };
};