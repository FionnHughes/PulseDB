#include <iostream>

#include "CollectorScheduler.h"
#include "CpuCollector.h"
#include "RamCollector.h"
#include "DiskCollector.h"
#include "NetworkCollector.h"
#include "ProcessCollector.h"
#include "SystemMetricsCollector.h"
#include "PowerCollector.h"

namespace pulsedb {
    CollectorScheduler::CollectorScheduler(SpscQueue<MetricSnapshot, 1024>& queue, RingBuffer<MetricSnapshot, 300>& ring):
        m_io(),
        m_timer(m_io),
        m_queue(queue),
        m_ring(ring),
        m_interval(1000),
        m_running(false)
    {
        m_collectors.push_back(std::make_unique<CpuCollector>());
        m_collectors.push_back(std::make_unique<RamCollector>());
        m_collectors.push_back(std::make_unique<DiskCollector>());
        m_collectors.push_back(std::make_unique<NetworkCollector>());
        m_collectors.push_back(std::make_unique<ProcessCollector>());
        m_collectors.push_back(std::make_unique<SystemMetricsCollector>());
        m_collectors.push_back(std::make_unique<PowerCollector>());

        for (auto& collector : m_collectors) {
            collector->initialize();
        }
    }

    void CollectorScheduler::start() {
        m_running = true;
        m_snapshot.reserve(16, 4, 4, 25);
        tick();
    }

    void CollectorScheduler::stop() {
        m_running = false;
        m_timer.cancel();
    }

    void CollectorScheduler::tick() {
        if (!m_running) {
            return;
        }
        auto start = std::chrono::steady_clock::now();
        m_snapshot.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        m_snapshot.cpu_per_core_percent.clear();
        m_snapshot.cpu_per_core_frequency_mhz.clear();
        m_snapshot.disks.clear();
        m_snapshot.network_adapters.clear();
        m_snapshot.top_processes.clear();

        for (auto& collector : m_collectors) {
            collector->collect();
        }

        for (auto& collector : m_collectors) {
            collector->fill_snapshot(m_snapshot);
        }
        if (!m_queue.try_push(m_snapshot)) {
            std::cerr << "Error pushing snapshot into queue\n";
        }
        m_ring.push(m_snapshot);

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        if (duration >= m_interval) {
            std::cerr << "Tick over budget by " << (duration - m_interval).count() << "ms\n";
            m_timer.expires_after(std::chrono::milliseconds(0));
        }
        else {
            m_timer.expires_after(m_interval - duration);
        }
        m_timer.async_wait([this](const boost::system::error_code& ec) {
            if (ec) return;
            tick();
         });
    }
}
