#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <boost/asio.hpp>
#include <thread>
#include <atomic>
#include <json/json.h>

#include "AlertRule.h"
#include "../queue/RingBuffer.h"
#include "../collector/MetricSnapshot.h"

namespace pulsedb {

    struct AlertHistoryEntry {
        int64_t id;
        int64_t rule_id;
        int64_t triggered_at;
        int64_t resolved_at;
        double  peak_value;
        int64_t duration_seconds;
    };

    class AlertEngine {
    public:
        bool open(const std::string& db_path, RingBuffer<MetricSnapshot, 300>* ring);
        void close();

        void start();
        void stop();

        // rest crud, all thread safe, can be called from the api thread
        std::vector<AlertRule> get_rules();
        int64_t add_rule(const AlertRule& rule);          // returns new id, -1 on fail
        bool update_rule(int64_t id, const AlertRule& rule);
        bool delete_rule(int64_t id);
        bool delete_history_entry(int64_t id);
        int delete_history_older_than(int64_t cutoff_ms);
        std::vector<AlertHistoryEntry> get_history(int limit, int offset, int64_t rule_id_filter);
        std::vector<Json::Value> get_active_states();

    private:
        bool create_tables();
        bool seed_test_rules();
        bool load_rules();

        void schedule_tick();

        void on_tick();
        bool evaluate_condition(const AlertRule& rule, double value);
        void fire_alert(const AlertRule& rule, AlertRuntimeState& state, double value);
        void resolve_alert(const AlertRule& rule, AlertRuntimeState& state);

        sqlite3* m_db{ nullptr };
        RingBuffer<MetricSnapshot, 300>* m_ring{ nullptr };

        std::vector<AlertRule> m_rules;
        std::unordered_map<int64_t, AlertRuntimeState> m_runtime_states;
        std::mutex m_rules_mutex;  // protects m_rules and m_runtime_states from api thread vs evaluator thread

        boost::asio::io_context m_ioc;
        boost::asio::steady_timer m_timer{ m_ioc };
        std::thread m_thread;
        std::atomic<bool> m_running{ false };
    };

    Json::Value rule_to_json(const AlertRule& r);
    AlertRule json_to_rule(const Json::Value& j);
}