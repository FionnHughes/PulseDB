#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "CollectorScheduler.h"
#include "CpuCollector.h"
#include "DiskCollector.h"
#include "NetworkCollector.h"
#include "PowerCollector.h"
#include "ProcessCollector.h"
#include "RamCollector.h"
#include "SystemMetricsCollector.h"

namespace pulsedb {
    // sets up the asio context, timer and signal handling, creates all collectors
    CollectorScheduler::CollectorScheduler(SpscQueue<MetricSnapshot, 1024>& queue, RingBuffer<MetricSnapshot, 300>& ring, int interval_ms)
        : m_io(), m_timer(m_io), m_signals(m_io, SIGINT, SIGTERM), m_queue(queue), m_ring(ring), m_interval(interval_ms), m_running(false) {
        // adding all collectors (order doesn't matter)
        m_collectors.push_back(std::make_unique<CpuCollector>());
        m_collectors.push_back(std::make_unique<RamCollector>());
        m_collectors.push_back(std::make_unique<DiskCollector>());
        m_collectors.push_back(std::make_unique<NetworkCollector>());
        m_collectors.push_back(std::make_unique<ProcessCollector>());
        m_process_collector = static_cast<ProcessCollector*>(m_collectors.back().get());
        m_collectors.push_back(std::make_unique<SystemMetricsCollector>());
        m_collectors.push_back(std::make_unique<PowerCollector>());

        // initialize all collectors before the first tick
        for (auto& collector : m_collectors) {
            collector->initialize();
        }
    }

    // pre-sizes the snapshot vectors based on actual hardware, arms signal handling, then starts the first tick
    void CollectorScheduler::start() {
        m_running = true;

        // gets the logical core count to pre allocate the per core vectors
#ifdef _WIN32
        DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        int core_count = n > 0 ? static_cast<int>(n) : 1;
#else
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        int core_count = n > 0 ? static_cast<int>(n) : 1;
#endif
        m_snapshot.reserve(core_count, 4, 4, 25);

        // ctrl+c / kill -> clean stop instead of dying mid write
        m_signals.async_wait([this](const boost::system::error_code& ec, int) {
            if (!ec)
                stop();
        });

        tick();
    }

    // cancels the timer and signals, shuts down collectors on the io thread, joins the io thread if there is one
    void CollectorScheduler::stop() {
        m_running = false;
        // done on the io thread so shutdown cant close a collectors files mid tick
        boost::asio::post(m_io, [this]() {
            m_timer.cancel();
            m_signals.cancel();
            for (auto& c : m_collectors) {
                c->shutdown();
            }
        });
        // signal handler runs on the io thread itself, joining yourself would throw
        if (m_io_thread.joinable() && std::this_thread::get_id() != m_io_thread.get_id())
            m_io_thread.join();
    }

    void CollectorScheduler::run() { m_io.run(); }

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
            if (ec)
                return;
            tick();
        });
    }
} // namespace pulsedb
