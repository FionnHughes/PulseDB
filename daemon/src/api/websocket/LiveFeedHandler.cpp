#include "LiveFeedHandler.h"
#include "../ApiServer.h"   // for snapshot_to_json
#include <drogon/drogon.h>

namespace pulsedb {
    // client connected and  track it so broadcast_tick can reach it
    void LiveFeedHandler::handleNewConnection(const drogon::HttpRequestPtr&, const drogon::WebSocketConnectionPtr& conn) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_connections.insert(conn);
    }

    // client gone so stoping trying to send to it
    void LiveFeedHandler::handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_connections.erase(conn);
    }

    // one way feed
    void LiveFeedHandler::handleNewMessage(const drogon::WebSocketConnectionPtr&, std::string&&, const drogon::WebSocketMessageType&) {
    }

    void LiveFeedHandler::init(RingBuffer<MetricSnapshot, 300>* ring) {
        s_ring = ring;
        drogon::app().getLoop()->runEvery(1.0, []() { broadcast_tick(); });
    }

    // fires every second and gets the latest snapshot and gives it out
    void LiveFeedHandler::broadcast_tick() {
        if (!s_ring) return;
        auto snap = s_ring->latest();
        if (!snap) return;

        Json::Value j = snapshot_to_json(*snap);
        Json::StreamWriterBuilder writer;
        std::string payload = Json::writeString(writer, j);

        std::lock_guard<std::mutex> lock(s_mutex);
        for (auto& conn : s_connections) {
            conn->send(payload);
        }
    }
}