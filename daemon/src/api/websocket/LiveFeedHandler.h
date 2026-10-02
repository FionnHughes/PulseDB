#pragma once
#include <drogon/WebSocketController.h>
#include <mutex>
#include <rapidjson/stringbuffer.h>
#include <unordered_set>

#include "../../collector/MetricSnapshot.h"
#include "../../queue/RingBuffer.h"

namespace pulsedb {
    // pushes one snapshot per second to every connected client
    class LiveFeedHandler : public drogon::WebSocketController<LiveFeedHandler> {
    public:
        void handleNewMessage(const drogon::WebSocketConnectionPtr&, std::string&&, const drogon::WebSocketMessageType&) override;
        void handleNewConnection(const drogon::HttpRequestPtr&, const drogon::WebSocketConnectionPtr&) override;
        void handleConnectionClosed(const drogon::WebSocketConnectionPtr&) override;

        WS_PATH_LIST_BEGIN
        WS_PATH_ADD("/ws/live", drogon::Get);
        WS_PATH_LIST_END

        // called once from ApiServer, gets us the ring buffer and starts the timer
        static void init(RingBuffer<MetricSnapshot, 300>* ring);

    private:
        static void broadcast_tick();

        // static since drogon owns the controller instance, not us
        static inline RingBuffer<MetricSnapshot, 300>* s_ring = nullptr;
        static inline std::mutex s_mutex;
        static inline std::unordered_set<drogon::WebSocketConnectionPtr> s_connections;

        // reused every tick
        static inline rapidjson::StringBuffer s_payload;
        static inline int64_t s_last_ts = 0;
    };
} // namespace pulsedb
