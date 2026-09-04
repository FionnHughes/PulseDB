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

    void ApiServer::set_process_collector(ProcessCollector* pc) {
        m_process_collector = pc;
    }

    void ApiServer::set_alert_engine(AlertEngine* engine) {
        m_alert_engine = engine;
    }

    // drogon blocks on run() so it needs its own thread so crash here doesn't kill the daemon silently
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
        register_alert_routes();   // <-- this line was missing

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

                    // ring can be empty right after startup, before the first tick lands
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

                    // only metrics with a currently open writer, not full history
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

                    // null instead of zeroed stats, so an empty range isnt mistaken for real zero data
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

        drogon::app().registerHandler(
            "/api/processes/latest",
            [this](const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

                    if (!m_process_collector) {
                        Json::Value err;
                        err["error"] = "process collector not available";
                        auto response = drogon::HttpResponse::newHttpJsonResponse(err);
                        response->setStatusCode(drogon::k503ServiceUnavailable);
                        callback(response);
                        return;
                    }

                    // full list, unsorted past the top 25 since only those are sorted by partial sort
                    const auto& processes = m_process_collector->get_all_processes();

                    Json::Value arr(Json::arrayValue);
                    for (const auto& p : processes) {
                        Json::Value pj;
                        pj["pid"] = p.pid;
                        pj["name"] = p.name;
                        pj["cpu_pct"] = p.cpu_percent;
                        pj["ram_bytes"] = p.ram_bytes;
                        pj["threads"] = p.thread_count;
                        pj["handles"] = p.handle_count;
                        arr.append(pj);
                    }

                    Json::Value j;
                    j["count"] = static_cast<int>(processes.size());
                    j["processes"] = arr;

                    auto response = drogon::HttpResponse::newHttpJsonResponse(j);
                    callback(response);
            },
            { drogon::Get }
        );
    }

    void ApiServer::register_alert_routes() {
        if (!m_alert_engine) return;  // stage 1/2 only, no crud wired in

        AlertEngine* engine = m_alert_engine;

        drogon::app().registerHandler("/api/alerts/rules",
            [engine](const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                    Json::Value arr(Json::arrayValue);
                    for (const auto& r : engine->get_rules()) arr.append(rule_to_json(r));
                    callback(drogon::HttpResponse::newHttpJsonResponse(arr));
            }, { drogon::Get });

        drogon::app().registerHandler("/api/alerts/rules",
            [engine](const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                    auto json = req->getJsonObject();
                    if (!json) {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::k400BadRequest);
                        callback(resp);
                        return;
                    }
                    AlertRule rule = json_to_rule(*json);
                    int64_t id = engine->add_rule(rule);
                    Json::Value result;
                    result["id"] = static_cast<Json::Int64>(id);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                    resp->setStatusCode(id > 0 ? drogon::k201Created : drogon::k500InternalServerError);
                    callback(resp);
            }, { drogon::Post });

        drogon::app().registerHandler("/api/alerts/rules/{1}",
            [engine](const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
                    auto json = req->getJsonObject();
                    if (!json) {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::k400BadRequest);
                        callback(resp);
                        return;
                    }
                    AlertRule rule = json_to_rule(*json);
                    bool ok = engine->update_rule(id, rule);
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(ok ? drogon::k200OK : drogon::k404NotFound);
                    callback(resp);
            }, { drogon::Put });

        drogon::app().registerHandler("/api/alerts/rules/{1}",
            [engine](const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {
                    bool ok = engine->delete_rule(id);
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(ok ? drogon::k200OK : drogon::k404NotFound);
                    callback(resp);
            }, { drogon::Delete });

        drogon::app().registerHandler("/api/alerts/history",
            [engine](const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                    int limit = 50, offset = 0;
                    int64_t rule_id_filter = 0;
                    if (auto p = req->getParameter("limit"); !p.empty()) limit = std::stoi(p);
                    if (auto p = req->getParameter("offset"); !p.empty()) offset = std::stoi(p);
                    if (auto p = req->getParameter("rule_id"); !p.empty()) rule_id_filter = std::stoll(p);

                    Json::Value arr(Json::arrayValue);
                    for (const auto& h : engine->get_history(limit, offset, rule_id_filter)) {
                        Json::Value j;
                        j["id"] = static_cast<Json::Int64>(h.id);
                        j["rule_id"] = static_cast<Json::Int64>(h.rule_id);
                        j["triggered_at"] = static_cast<Json::Int64>(h.triggered_at);
                        j["resolved_at"] = static_cast<Json::Int64>(h.resolved_at);
                        j["peak_value"] = h.peak_value;
                        j["duration_seconds"] = static_cast<Json::Int64>(h.duration_seconds);
                        arr.append(j);
                    }
                    callback(drogon::HttpResponse::newHttpJsonResponse(arr));
            }, { drogon::Get });
    }
}