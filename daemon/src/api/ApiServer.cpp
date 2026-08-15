#include <drogon/drogon.h>

#include "websocket/LiveFeedHandler.h"
#include "storage/Downsampler.h"
#include "ApiServer.h"

namespace pulsedb {

    // converts a MetricSnapshot into the JSON shape for later
    Json::Value snapshot_to_json(const MetricSnapshot& snap) {
        Json::Value j;
        j["ts"] = snap.timestamp_ms;
        j["cpu_total"] = snap.cpu_total_percent;
        Json::Value cores(Json::arrayValue);
        for (auto v : snap.cpu_per_core_percent) cores.append(v);
        j["cpu_cores"] = cores;
        j["ram_used_bytes"] = snap.ram_used_bytes;
        j["ram_available_bytes"] = snap.ram_available_bytes;
        j["pulsedb_pid"] = snap.pulsedb_pid;
        j["pulsedb_cpu_pct"] = snap.pulsedb_cpu_percent;
        j["pulsedb_ram_bytes"] = snap.pulsedb_ram_bytes;
        return j;
    }

    ApiServer::ApiServer(StorageEngine& storage, RingBuffer<MetricSnapshot, 300>& ring, uint16_t port) :
        m_storage(storage),
        m_ring(ring),
        m_port(port),
        m_start_time(std::chrono::steady_clock::now()) { };

    void ApiServer::set_shutdown_callback(std::function<void()> cb) {
        m_shutdown_callback = std::move(cb);
    }

    void ApiServer::start() {
        m_thread = std::thread([this]() {
            try {
                run();
            }
            catch (const std::exception& e) {
                std::cerr << "ApiServer thread crashed: " << e.what() << "\n";
            }
            catch (...) {
                std::cerr << "ApiServer thread crashed with unknown exception\n";
            }
            });
    }

    void ApiServer::stop() {
        drogon::app().quit();
        if (m_thread.joinable()) m_thread.join();
    }

    // sets up the listener and routes, then blocks until quit() is called from stop()
    void ApiServer::run() {
        std::cout << "ApiServer: adding listener on port " << m_port << "\n";
        drogon::app().addListener("127.0.0.1", m_port);
        std::cout << "ApiServer: registering routes\n";
        register_routes();

        drogon::app().setIntSignalHandler([this]() {
            if (m_shutdown_callback) m_shutdown_callback();
        });
        LiveFeedHandler::init(&m_ring);

        std::cout << "ApiServer: starting drogon event loop\n";
        drogon::app().run();
    }

    void ApiServer::register_routes() {
        drogon::app().registerHandler(
            "/api/status",
            [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                
                Json::Value j;

                auto elapsed = std::chrono::steady_clock::now() - m_start_time;
                j["uptime_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                j["version"] = "0.1.0"; // hardcoded for now

                auto response = drogon::HttpResponse::newHttpJsonResponse(j);
                callback(response);
            },
            { drogon::Get }
        );

        drogon::app().registerHandler(
            "/api/latest",
            [this](const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

                    auto snap = m_ring.latest();
                    if (!snap) {
                        Json::Value err;
                        err["error"] = "no data yet";

                        auto response = drogon::HttpResponse::newHttpJsonResponse(err);
                        response->setStatusCode(drogon::k404NotFound);
                        callback(response);
                    }
                    else {
                        Json::Value j = snapshot_to_json(*snap);
                        auto response = drogon::HttpResponse::newHttpJsonResponse(j);
                        callback(response);
                    }
            },
            { drogon::Get }
        );

        drogon::app().registerHandler(
            "/api/metrics",
            [this](const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

                    auto metrics = m_storage.get_active_metrics();

                    Json::Value arr(Json::arrayValue);
                    for (const auto& name : metrics) arr.append(name);

                    Json::Value j;
                    j["metrics"] = arr;

                    auto response = drogon::HttpResponse::newHttpJsonResponse(j);
                    callback(response);
            },
            { drogon::Get }
        );

        drogon::app().registerHandler(
            "/api/query",
            [this](const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

                    auto metric = req->getParameter("metric");
                    auto from_str = req->getParameter("from");
                    auto to_str = req->getParameter("to");

                    if (metric.empty() || from_str.empty() || to_str.empty()) {
                        Json::Value err;
                        err["error"] = "missing required parameter: metric, from, and to are all required";

                        auto response = drogon::HttpResponse::newHttpJsonResponse(err);
                        response->setStatusCode(drogon::k400BadRequest);
                        callback(response);
                        return;
                    }

                    int64_t from_ms, to_ms;
                    try {
                        from_ms = std::stoll(from_str);
                        to_ms = std::stoll(to_str);
                    }
                    catch (...) {
                        Json::Value err;
                        err["error"] = "from and to must be valid integers";

                        auto response = drogon::HttpResponse::newHttpJsonResponse(err);
                        response->setStatusCode(drogon::k400BadRequest);
                        callback(response);
                        return;
                    }

                    auto results = m_storage.query(metric, from_ms, to_ms);

                    Json::Value data(Json::arrayValue);
                    for (const auto& reading : results) {
                        Json::Value point;
                        point["ts"] = reading.timestamp_ms;
                        point["value"] = reading.value;
                        data.append(point);
                    }

                    Json::Value j;
                    j["metric"] = metric;
                    j["resolution"] = "raw";  // hardcoded for now
                    j["from"] = from_ms;
                    j["to"] = to_ms;
                    j["count"] = static_cast<int>(results.size());
                    j["data"] = data;

                    Json::Value stats_json;
                    if (results.empty()) {
                        stats_json = Json::Value(Json::nullValue);
                    }
                    else {
                        auto stats = Downsampler::compute_stats(results);
                        stats_json["min"] = stats.min;
                        stats_json["max"] = stats.max;
                        stats_json["mean"] = stats.mean;
                        stats_json["p95"] = stats.p95;
                    }

                    j["stats"] = stats_json;

                    auto response = drogon::HttpResponse::newHttpJsonResponse(j);
                    callback(response);
            },
            { drogon::Get }
        );
    }
}