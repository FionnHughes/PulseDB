#pragma once

#include <cstdint>
#include <thread>
#include <chrono>
#include <json/json.h>
#include <functional>

#include "../storage/StorageEngine.h"
#include "../queue/RingBuffer.h"
#include "../collector/MetricSnapshot.h"
#include "../collector/ProcessCollector.h"
#include "../alerts/AlertEngine.h"
#include "../common/Config.h"

namespace pulsedb {

    Json::Value snapshot_to_json(const MetricSnapshot& snap);

    class ApiServer {
    public:
        ApiServer(StorageEngine& storage, RingBuffer<MetricSnapshot, 300>& ring, uint16_t port);

        void set_shutdown_callback(std::function<void()> cb);
        void set_process_collector(ProcessCollector* pc);
        void set_alert_engine(AlertEngine* engine);
        void set_config(const Config& cfg);

        void start();
        void stop();

    private:
        StorageEngine& m_storage;
        RingBuffer<MetricSnapshot, 300>& m_ring;

        ProcessCollector* m_process_collector = nullptr;
        AlertEngine* m_alert_engine = nullptr;
        Config m_config;

        uint16_t m_port;
        std::thread m_thread;
        std::chrono::steady_clock::time_point m_start_time;
        std::function<void()> m_shutdown_callback;

        void run();
        void register_routes();
        void register_alert_routes();  // split out since it's a chunk of new endpoints
    };
}