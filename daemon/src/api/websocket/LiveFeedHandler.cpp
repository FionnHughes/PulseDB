#include "LiveFeedHandler.h"
#include "../ApiServer.h" // for snapshot_to_json
#include <drogon/drogon.h>

namespace pulsedb {
    // new client, track it so broadcast_tick can reach it
    void LiveFeedHandler::handleNewConnection(const drogon::HttpRequestPtr&, const drogon::WebSocketConnectionPtr& conn) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_connections.insert(conn);
    }

    // client gone, stop trying to send to it
    void LiveFeedHandler::handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_connections.erase(conn);
    }

    // one way feed
    void LiveFeedHandler::handleNewMessage(const drogon::WebSocketConnectionPtr&, std::string&&, const drogon::WebSocketMessageType&) {}

    void LiveFeedHandler::init(RingBuffer<MetricSnapshot, 300>* ring) {
        s_ring = ring;
        drogon::app().getLoop()->runEvery(1.0, []() { broadcast_tick(); });
    }

    // fires every second and gets the latest snapshot and gives it out
    void LiveFeedHandler::broadcast_tick() {
        if (!s_ring)
            return;

        // nobody connected so no point building the json
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_connections.empty())
                return;
        }

        auto snap = s_ring->latest();
        if (!snap)
            return;

        // the collector hasn't made a new snapshot since the last send
        if (snap->timestamp_ms == s_last_ts)
            return;
        s_last_ts = snap->timestamp_ms;

        s_payload.Clear(); // keeps its memory
        snapshot_to_json(*snap, s_payload);

        std::lock_guard<std::mutex> lock(s_mutex);
        for (auto& conn : s_connections) {
            conn->send(s_payload.GetString(), s_payload.GetSize());
        }
    }
} // namespace pulsedb
