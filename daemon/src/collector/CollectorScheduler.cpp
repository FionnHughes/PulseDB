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
    // sets up the asio context and timer, creates all collectors
    CollectorScheduler::CollectorScheduler(SpscQueue<MetricSnapshot, 1024>& queue, RingBuffer<MetricSnapshot, 300>& ring):
        m_io(),
        m_timer(m_io),
        m_queue(queue),
        m_ring(ring),
        m_interval(1000),
        m_running(false)
    {
        // adding all collectors (order doesn't matter)
        m_collectors.push_back(std::make_unique<CpuCollector>());
        m_collectors.push_back(std::make_unique<RamCollector>());
        m_collectors.push_back(std::make_unique<DiskCollector>());
        m_collectors.push_back(std::make_unique<NetworkCollector>());
        m_collectors.push_back(std::make_unique<ProcessCollector>());
        m_collectors.push_back(std::make_unique<SystemMetricsCollector>());
        m_collectors.push_back(std::make_unique<PowerCollector>());

        // initialize all collectors before the first tick
        for (auto& collector : m_collectors) {
            collector->initialize();
        }
    }

    // pre-sizes the snapshot vectors based on actual hardware then starts the first tick
    void CollectorScheduler::start() {
        m_running = true;

        // gets amount of processors we have from windows to pre allocate the per core vectors
        DWORD core_count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        if (core_count == 0) core_count = 1;
        m_snapshot.reserve(static_cast<int>(core_count), 4, 4, 25);
        tick();
    }

    // cancels the timer, joins the io thread, then shuts down all collectors
    void CollectorScheduler::stop() {
        m_running = false;
        boost::asio::post(m_io, [this]() { m_timer.cancel(); });
        if (m_io_thread.joinable()) m_io_thread.join();
        for (auto& c : m_collectors) {
            c->shutdown();
        }
    }

    void CollectorScheduler::run() {
        m_io.run();
    }

    void CollectorScheduler::run_async() {
        m_io_thread = std::thread([this]() { m_io.run(); });
    }

    // one collection cycle which runs all collectors, fills the snapshot, pushes to queue and ring buffer, then schedules the next tick
    void CollectorScheduler::tick() {
        if (!m_running) {
            return;
        }
        // timing the tick so we can subtract it from the interval and keep the real intervals accurate
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

        // if the tick took longer than 1 second, log it and fire the next one instantly (to try to catch back up)
        if (duration >= m_interval) {
            std::cerr << "Tick over budget by " << (duration - m_interval).count() << "ms\n";
            m_timer.expires_after(std::chrono::milliseconds(0));
        }
        else {
            // schedule next tick after the remaining time in this interval
            m_timer.expires_after(m_interval - duration);
        }
        m_timer.async_wait([this](const boost::system::error_code& ec) {
            if (ec) return;
            tick();
         });
    }
}
