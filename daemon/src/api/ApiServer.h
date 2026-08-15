#pragma once

#include <cstdint>
#include <thread>
#include <chrono>
#include <json/json.h>
#include <functional>

#include "../storage/StorageEngine.h"
#include "../queue/RingBuffer.h"
#include "../collector/MetricSnapshot.h"

namespace pulsedb {

    Json::Value snapshot_to_json(const MetricSnapshot& snap);

    class ApiServer {
    public:
        ApiServer(StorageEngine& storage, RingBuffer<MetricSnapshot, 300>& ring, uint16_t port);

        void set_shutdown_callback(std::function<void()> cb);

        void start();
        void stop();

    private:
        StorageEngine& m_storage;
        RingBuffer<MetricSnapshot, 300>& m_ring;
        uint16_t m_port;
        std::thread m_thread;
        std::chrono::steady_clock::time_point m_start_time;
        std::function<void()> m_shutdown_callback;

        // the function that actually calls drogon::app().run(), executed on m_thread
        void run();  
        void register_routes();
    };
}