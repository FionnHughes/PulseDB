#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

// single-producer single-consumer lock-free queue where the collector pushes, the writer thread pops
template<typename T, size_t Capacity>
class SpscQueue {
public:
	// returns false if the queue is full and the caller decides whether to retry or drop
	bool try_push(const T& item) { return try_push_impl(item); }
	bool try_push(T&& item) { return try_push_impl(std::move(item)); }

	// returns nullopt if the queue is empty
	std::optional<T> try_pop() {
		size_t current_read = m_read_idx.load(std::memory_order_relaxed);
		size_t current_write = m_write_idx.load(std::memory_order_acquire);
		if (current_read == current_write) {
			return std::nullopt;
		}
		T item = std::move(m_storage[current_read]);
		// wastes one slot deliberately to distinguish "empty" from "full" without a separate size counter
		size_t next = (current_read + 1) % Capacity;
		m_read_idx.store(next, std::memory_order_release);

		return item;
	}

	size_t size() const {
		size_t current_read = m_read_idx.load(std::memory_order_relaxed);
		size_t current_write = m_write_idx.load(std::memory_order_relaxed);

		return (current_write - current_read + Capacity) % Capacity;
	}

	bool empty() const {
		return m_read_idx.load(std::memory_order_relaxed) == m_write_idx.load(std::memory_order_relaxed);
	}

private:
	template<typename U>
	bool try_push_impl(U&& item) {
		size_t current_write = m_write_idx.load(std::memory_order_relaxed);
		size_t next = (current_write + 1) % Capacity;
		// full so advancing write would catch up to read
		if (next == m_read_idx.load(std::memory_order_acquire)) {
			return false;
		}
		m_storage[current_write] = std::forward<U>(item);
		m_write_idx.store(next, std::memory_order_release);
		return true;
	}
	std::array<T, Capacity> m_storage;
	// each on its own cache line to prevent bad sharing between producer and consumer threads
	alignas(64) std::atomic<size_t> m_write_idx{ 0 };
	alignas(64) std::atomic<size_t> m_read_idx{ 0 };
};