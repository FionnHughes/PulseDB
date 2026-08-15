#pragma once

#include <atomic>
#include <vector>
#include <memory>
#include <chrono>
#include <boost/asio.hpp>

#include "IMetricCollector.h"
#include "MetricSnapshot.h"
#include "ProcessCollector.h"
#include "../queue/SpscQueue.h"
#include "../queue/RingBuffer.h"

namespace pulsedb {

    // owns all the collectors and runs them every second using a boost asio timer
    class  CollectorScheduler {
    public:
        CollectorScheduler(
            SpscQueue<MetricSnapshot, 1024>& queue,
            RingBuffer<MetricSnapshot, 300>& ring
        );
        void start();
        void stop();
        void run();
        void run_async();

        ProcessCollector* get_process_collector() const { return m_process_collector; }

    private:
        // all active collectors and are run every tick
        std::vector<std::unique_ptr<IMetricCollector>> m_collectors;

        // asio event loop that drives the timer
        boost::asio::io_context m_io;
        boost::asio::steady_timer m_timer;
        std::thread m_io_thread;

        // reused every tick - vecctors get cleared at the start of each tick to avoid stale data
        MetricSnapshot m_snapshot;
        SpscQueue<MetricSnapshot, 1024>& m_queue;
        RingBuffer<MetricSnapshot, 300>& m_ring;

        ProcessCollector* m_process_collector = nullptr;

        // target tick interval, 1000ms
        std::chrono::milliseconds m_interval;

        // checked at the start of each tick
        std::atomic<bool> m_running;

        void tick();
    };
}
