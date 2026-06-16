#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>
#include <algorithm>

// circular buffer which keeps the last Capacity snapshots in memory so the live API can read recent data without touching disk
template<typename T, size_t Capacity>
class RingBuffer {
public:
    // overwrites the oldest entry when full with no blocking, just rolling
    void push(const T& item) {
        // load the write position first
        size_t idx = m_write_idx.load(std::memory_order_relaxed);

        // write the data before advancing the index
        m_storage[idx % Capacity] = item;

        // release ordering ensures any thread that reads this index also sees the write above
        m_write_idx.store(idx + 1, std::memory_order_release);
    }

    // used to move item rather than copying
    void push(T&& item) {
        size_t idx = m_write_idx.load(std::memory_order_relaxed);
        m_storage[idx % Capacity] = std::move(item);
        m_write_idx.store(idx + 1, std::memory_order_release);
    }

    // returns the most recently pushed item potentially useful for live dashboard reads
    std::optional<T> latest() {
        size_t write_idx = m_write_idx.load(std::memory_order_acquire);
        if (write_idx == 0) {
            return std::nullopt;
        }
        return m_storage[(write_idx - 1) % Capacity];
    }

    size_t size() const {
        return std::min(m_write_idx.load(std::memory_order_relaxed), Capacity);
    }

    // 0 is the most recent, 1 is one before that etc
    T get(size_t index) {
        size_t write_idx = m_write_idx.load(std::memory_order_acquire);
        assert(index < write_idx);
        return m_storage[(write_idx - 1 - index) % Capacity];
    }

private:
    std::array<T, Capacity> m_storage;
    alignas(64) std::atomic<size_t> m_write_idx{ 0 };
};