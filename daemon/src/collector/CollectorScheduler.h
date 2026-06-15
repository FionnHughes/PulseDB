#pragma once

#include <vector>
#include <memory>
#include <chrono>
#include <boost/asio.hpp>

#include "IMetricCollector.h"
#include "MetricSnapshot.h"
#include "../queue/SpscQueue.h"
#include "../queue/RingBuffer.h"

namespace pulsedb {

    class  CollectorScheduler {
    public:
        CollectorScheduler(
            SpscQueue<MetricSnapshot, 1024>& queue,
            RingBuffer<MetricSnapshot, 300>& ring
        );
        void start();
        void stop();

    private:
        std::vector<std::unique_ptr<IMetricCollector>> m_collectors;
        boost::asio::io_context m_io;
        boost::asio::steady_timer m_timer;
        MetricSnapshot m_snapshot;
        SpscQueue<MetricSnapshot, 1024>& m_queue;
        RingBuffer<MetricSnapshot, 300>& m_ring;
        std::chrono::milliseconds m_interval;
        bool m_running;

        void tick();
    };
}
