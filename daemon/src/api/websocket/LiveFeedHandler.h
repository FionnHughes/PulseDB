#pragma once
#include <drogon/WebSocketController.h>
#include <mutex>
#include <unordered_set>
#include "../../queue/RingBuffer.h"
#include "../../collector/MetricSnapshot.h"

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
    };
}