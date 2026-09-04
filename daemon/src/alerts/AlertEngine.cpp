#include "AlertEngine.h"
#include "MetricExtractor.h"
#include "ToastNotifier.h"
#include <iostream>
#include <chrono>

namespace pulsedb {

    using namespace std::chrono;

    bool AlertEngine::open(const std::string& db_path, RingBuffer<MetricSnapshot, 300>* ring) {
        m_ring = ring;
        if (sqlite3_open(db_path.c_str(), &m_db) != SQLITE_OK) {
            std::cerr << "AlertEngine: failed to open db\n";
            return false;
        }
        if (!create_tables()) return false;
        if (!seed_test_rules()) return false;
        if (!load_rules()) return false;
        return true;
    }

    void AlertEngine::close() {
        stop();
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }

    bool AlertEngine::create_tables() {
        const char* rules_sql =
            "CREATE TABLE IF NOT EXISTS alert_rules ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL,"
            "metric TEXT NOT NULL,"
            "rule_type TEXT NOT NULL CHECK(rule_type IN ('threshold','sustained_threshold','rate_of_change')),"
            "operator TEXT CHECK(operator IN ('gt','lt','gte','lte')),"
            "value REAL,"
            "duration_readings INTEGER,"
            "change_percent REAL,"
            "window_readings INTEGER,"
            "cooldown_seconds INTEGER NOT NULL DEFAULT 300,"
            "enabled INTEGER NOT NULL DEFAULT 1,"
            "created_at INTEGER NOT NULL);";

        const char* history_sql =
            "CREATE TABLE IF NOT EXISTS alert_history ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "rule_id INTEGER NOT NULL REFERENCES alert_rules(id),"
            "triggered_at INTEGER NOT NULL,"
            "resolved_at INTEGER,"
            "peak_value REAL,"
            "duration_seconds INTEGER);";

        char* err = nullptr;
        if (sqlite3_exec(m_db, rules_sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::cerr << "create alert_rules failed: " << err << "\n";
            sqlite3_free(err);
            return false;
        }
        if (sqlite3_exec(m_db, history_sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::cerr << "create alert_history failed: " << err << "\n";
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    bool AlertEngine::seed_test_rules() {
        // don't reseed if rules already exist
        const char* check_sql = "SELECT COUNT(*) FROM alert_rules;";
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(m_db, check_sql, -1, &stmt, nullptr);
        sqlite3_step(stmt);
        int count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        if (count > 0) return true;

        const char* insert_sql =
            "INSERT INTO alert_rules (name, metric, rule_type, operator, value, "
            "duration_readings, change_percent, window_readings, cooldown_seconds, enabled, created_at) VALUES "
            "('CPU high', 'cpu_total_percent', 'threshold', 'gt', 90.0, NULL, NULL, NULL, 300, 1, strftime('%s','now')*1000),"
            "('RAM sustained high', 'ram_used_bytes', 'sustained_threshold', 'gt', 8000000000, 6, NULL, NULL, 300, 1, strftime('%s','now')*1000);";

        char* err = nullptr;
        if (sqlite3_exec(m_db, insert_sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::cerr << "seed rules failed: " << err << "\n";
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    bool AlertEngine::load_rules() {
        const char* sql = "SELECT id, name, metric, rule_type, operator, value, duration_readings, "
            "change_percent, window_readings, cooldown_seconds, enabled, created_at FROM alert_rules;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        std::vector<AlertRule> new_rules;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AlertRule r;
            r.id = sqlite3_column_int64(stmt, 0);
            r.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            r.metric = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

            std::string type_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            if (type_str == "threshold") r.rule_type = RuleType::Threshold;
            else if (type_str == "sustained_threshold") r.rule_type = RuleType::SustainedThreshold;
            else r.rule_type = RuleType::RateOfChange;

            std::string op_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (op_str == "gt") r.op = Operator::Gt;
            else if (op_str == "lt") r.op = Operator::Lt;
            else if (op_str == "gte") r.op = Operator::Gte;
            else r.op = Operator::Lte;

            r.value = sqlite3_column_double(stmt, 5);
            r.duration_readings = sqlite3_column_int(stmt, 6);
            r.change_percent = sqlite3_column_double(stmt, 7);
            r.window_readings = sqlite3_column_int(stmt, 8);
            r.cooldown_seconds = sqlite3_column_int(stmt, 9);
            r.enabled = sqlite3_column_int(stmt, 10) != 0;
            r.created_at = sqlite3_column_int64(stmt, 11);

            new_rules.push_back(r);
        }
        sqlite3_finalize(stmt);

        std::lock_guard<std::mutex> lock(m_rules_mutex);
        m_rules = std::move(new_rules);
        for (const auto& r : m_rules) {
            if (m_runtime_states.find(r.id) == m_runtime_states.end()) {
                m_runtime_states[r.id] = AlertRuntimeState{ r.id };
            }
        }
        return true;
    }

    void AlertEngine::start() {
        m_running = true;
        schedule_tick();
        m_thread = std::thread([this] {
            try {
                winrt::init_apartment();
                m_ioc.run();
            }
            catch (const std::exception& e) {
                std::cerr << "AlertEngine thread crashed: " << e.what() << "\n";
            }
            catch (...) {
                std::cerr << "AlertEngine thread crashed with unknown exception\n";
            }
            });
    }

    void AlertEngine::schedule_tick() {
        m_timer.expires_after(std::chrono::seconds(5));
        m_timer.async_wait([this](const boost::system::error_code&) {
            try {
                on_tick();
            }
            catch (const std::exception& e) {
                std::cerr << "AlertEngine on_tick crashed: " << e.what() << "\n";
            }
            catch (...) {
                std::cerr << "AlertEngine on_tick crashed with unknown exception\n";
            }
            if (m_running) schedule_tick();
            });
    }

    void AlertEngine::stop() {
        m_running = false;
        m_timer.cancel();
        m_ioc.stop();
        if (m_thread.joinable()) m_thread.join();
    }

    void AlertEngine::on_tick() {
        if (m_ring->size() == 0) return;
        auto snap = m_ring->latest();
        if (!snap) return;
        const MetricSnapshot& latest = *snap;

        std::vector<AlertRule> rules_copy;
        {
            std::lock_guard<std::mutex> lock(m_rules_mutex);
            rules_copy = m_rules;
        }

        for (const auto& rule : rules_copy) {
            if (!rule.enabled) continue;

            auto value_opt = extract_metric_value(latest, rule.metric);
            if (!value_opt) continue;
            double value = *value_opt;

            bool condition_met = evaluate_condition(rule, value);
            int64_t now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

            std::lock_guard<std::mutex> lock(m_rules_mutex);
            AlertRuntimeState& state = m_runtime_states[rule.id];

            switch (state.state) {
            case AlertState::Inactive:
                if (condition_met) {
                    state.state = AlertState::Pending;
                    state.state_entered_at_ms = now;
                    state.consecutive_count = 1;
                }
                break;

            case AlertState::Pending:
                if (!condition_met) {
                    state.state = AlertState::Inactive;
                    state.consecutive_count = 0;
                }
                else if (rule.rule_type != RuleType::SustainedThreshold) {
                    fire_alert(rule, state, value);
                }
                else {
                    state.consecutive_count++;
                    if (state.consecutive_count >= rule.duration_readings) {
                        fire_alert(rule, state, value);
                    }
                }
                break;

            case AlertState::Active:
                if (condition_met) {
                    if (value > state.peak_value) state.peak_value = value;
                }
                else {
                    resolve_alert(rule, state);
                }
                break;

            case AlertState::Cooldown:
                if (now - state.state_entered_at_ms >= rule.cooldown_seconds * 1000) {
                    state.state = AlertState::Inactive;
                }
                break;
            }
        }
    }

    bool AlertEngine::evaluate_condition(const AlertRule& rule, double value) {
        if (rule.rule_type == RuleType::RateOfChange) {
            size_t window = static_cast<size_t>(rule.window_readings);
            if (window == 0 || window >= m_ring->size()) return false;

            const MetricSnapshot& newest_snap = m_ring->get(0);
            const MetricSnapshot& oldest_snap = m_ring->get(window - 1);

            auto newest = extract_metric_value(newest_snap, rule.metric);
            auto oldest = extract_metric_value(oldest_snap, rule.metric);
            if (!newest || !oldest || *oldest == 0.0) return false;

            double pct_change = ((*newest - *oldest) / *oldest) * 100.0;
            return std::abs(pct_change) >= rule.change_percent;
        }

        switch (rule.op) {
        case Operator::Gt:  return value > rule.value;
        case Operator::Lt:  return value < rule.value;
        case Operator::Gte: return value >= rule.value;
        case Operator::Lte: return value <= rule.value;
        }
        return false;
    }

    void AlertEngine::fire_alert(const AlertRule& rule, AlertRuntimeState& state, double value) {
        state.state = AlertState::Active;
        state.state_entered_at_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        state.peak_value = value;

        std::cout << "[ALERT] " << rule.name << " fired, value=" << value << "\n";

        std::string body = rule.metric + " = " + std::to_string(value);
        ToastNotifier::show("PulseDB Alert: " + rule.name, body);
    }

    void AlertEngine::resolve_alert(const AlertRule& rule, AlertRuntimeState& state) {
        int64_t now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        int64_t duration_sec = (now - state.state_entered_at_ms) / 1000;

        const char* sql = "INSERT INTO alert_history (rule_id, triggered_at, resolved_at, peak_value, duration_seconds) "
            "VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, rule.id);
        sqlite3_bind_int64(stmt, 2, state.state_entered_at_ms);
        sqlite3_bind_int64(stmt, 3, now);
        sqlite3_bind_double(stmt, 4, state.peak_value);
        sqlite3_bind_int64(stmt, 5, duration_sec);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        state.state = AlertState::Cooldown;
        state.state_entered_at_ms = now;
    }

    std::vector<AlertRule> AlertEngine::get_rules() {
        std::lock_guard<std::mutex> lock(m_rules_mutex);
        return m_rules;
    }

    int64_t AlertEngine::add_rule(const AlertRule& rule) {
        const char* sql = "INSERT INTO alert_rules (name, metric, rule_type, operator, value, "
            "duration_readings, change_percent, window_readings, cooldown_seconds, enabled, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;

        std::string type_str = rule.rule_type == RuleType::Threshold ? "threshold"
            : rule.rule_type == RuleType::SustainedThreshold ? "sustained_threshold" : "rate_of_change";
        std::string op_str = rule.op == Operator::Gt ? "gt" : rule.op == Operator::Lt ? "lt"
            : rule.op == Operator::Gte ? "gte" : "lte";
        int64_t now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        sqlite3_bind_text(stmt, 1, rule.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, rule.metric.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, type_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, op_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 5, rule.value);
        sqlite3_bind_int(stmt, 6, rule.duration_readings);
        sqlite3_bind_double(stmt, 7, rule.change_percent);
        sqlite3_bind_int(stmt, 8, rule.window_readings);
        sqlite3_bind_int(stmt, 9, rule.cooldown_seconds);
        sqlite3_bind_int(stmt, 10, rule.enabled ? 1 : 0);
        sqlite3_bind_int64(stmt, 11, now);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return -1;
        }
        int64_t new_id = sqlite3_last_insert_rowid(m_db);
        sqlite3_finalize(stmt);

        load_rules();
        return new_id;
    }

    bool AlertEngine::update_rule(int64_t id, const AlertRule& rule) {
        const char* sql = "UPDATE alert_rules SET name=?, metric=?, rule_type=?, operator=?, value=?, "
            "duration_readings=?, change_percent=?, window_readings=?, cooldown_seconds=?, enabled=? WHERE id=?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        std::string type_str = rule.rule_type == RuleType::Threshold ? "threshold"
            : rule.rule_type == RuleType::SustainedThreshold ? "sustained_threshold" : "rate_of_change";
        std::string op_str = rule.op == Operator::Gt ? "gt" : rule.op == Operator::Lt ? "lt"
            : rule.op == Operator::Gte ? "gte" : "lte";

        sqlite3_bind_text(stmt, 1, rule.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, rule.metric.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, type_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, op_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 5, rule.value);
        sqlite3_bind_int(stmt, 6, rule.duration_readings);
        sqlite3_bind_double(stmt, 7, rule.change_percent);
        sqlite3_bind_int(stmt, 8, rule.window_readings);
        sqlite3_bind_int(stmt, 9, rule.cooldown_seconds);
        sqlite3_bind_int(stmt, 10, rule.enabled ? 1 : 0);
        sqlite3_bind_int64(stmt, 11, id);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (ok) {
            load_rules();
            std::lock_guard<std::mutex> lock(m_rules_mutex);
            m_runtime_states[id] = AlertRuntimeState{ id };
        }
        return ok;
    }

    bool AlertEngine::delete_rule(int64_t id) {
        const char* sql = "DELETE FROM alert_rules WHERE id=?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int64(stmt, 1, id);
        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (ok) {
            load_rules();
            std::lock_guard<std::mutex> lock(m_rules_mutex);
            m_runtime_states.erase(id);
        }
        return ok;
    }

    std::vector<AlertHistoryEntry> AlertEngine::get_history(int limit, int offset, int64_t rule_id_filter) {
        std::vector<AlertHistoryEntry> results;
        std::string sql = "SELECT id, rule_id, triggered_at, resolved_at, peak_value, duration_seconds FROM alert_history";
        if (rule_id_filter > 0) sql += " WHERE rule_id = ?";
        sql += " ORDER BY triggered_at DESC LIMIT ? OFFSET ?;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return results;

        int bind_idx = 1;
        if (rule_id_filter > 0) sqlite3_bind_int64(stmt, bind_idx++, rule_id_filter);
        sqlite3_bind_int(stmt, bind_idx++, limit);
        sqlite3_bind_int(stmt, bind_idx++, offset);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AlertHistoryEntry e;
            e.id = sqlite3_column_int64(stmt, 0);
            e.rule_id = sqlite3_column_int64(stmt, 1);
            e.triggered_at = sqlite3_column_int64(stmt, 2);
            e.resolved_at = sqlite3_column_int64(stmt, 3);
            e.peak_value = sqlite3_column_double(stmt, 4);
            e.duration_seconds = sqlite3_column_int64(stmt, 5);
            results.push_back(e);
        }
        sqlite3_finalize(stmt);
        return results;
    }

    Json::Value rule_to_json(const AlertRule& r) {
        Json::Value j;
        j["id"] = static_cast<Json::Int64>(r.id);
        j["name"] = r.name;
        j["metric"] = r.metric;
        j["rule_type"] = r.rule_type == RuleType::Threshold ? "threshold"
            : r.rule_type == RuleType::SustainedThreshold ? "sustained_threshold" : "rate_of_change";
        j["operator"] = r.op == Operator::Gt ? "gt" : r.op == Operator::Lt ? "lt"
            : r.op == Operator::Gte ? "gte" : "lte";
        j["value"] = r.value;
        j["duration_readings"] = r.duration_readings;
        j["change_percent"] = r.change_percent;
        j["window_readings"] = r.window_readings;
        j["cooldown_seconds"] = r.cooldown_seconds;
        j["enabled"] = r.enabled;
        j["created_at"] = static_cast<Json::Int64>(r.created_at);
        return j;
    }

    AlertRule json_to_rule(const Json::Value& j) {
        AlertRule r;
        r.name = j.get("name", "").asString();
        r.metric = j.get("metric", "").asString();

        std::string type_str = j.get("rule_type", "threshold").asString();
        r.rule_type = type_str == "sustained_threshold" ? RuleType::SustainedThreshold
            : type_str == "rate_of_change" ? RuleType::RateOfChange : RuleType::Threshold;

        std::string op_str = j.get("operator", "gt").asString();
        r.op = op_str == "lt" ? Operator::Lt : op_str == "gte" ? Operator::Gte
            : op_str == "lte" ? Operator::Lte : Operator::Gt;

        r.value = j.get("value", 0.0).asDouble();
        r.duration_readings = j.get("duration_readings", 0).asInt();
        r.change_percent = j.get("change_percent", 0.0).asDouble();
        r.window_readings = j.get("window_readings", 0).asInt();
        r.cooldown_seconds = j.get("cooldown_seconds", 300).asInt();
        r.enabled = j.get("enabled", true).asBool();
        return r;
    }
}