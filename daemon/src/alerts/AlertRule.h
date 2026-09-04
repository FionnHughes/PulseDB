#pragma once

#include <cstdint>
#include <string>

namespace pulsedb {

    enum class RuleType {
        Threshold,
        SustainedThreshold,
        RateOfChange
    };

    enum class Operator {
        Gt,
        Lt,
        Gte,
        Lte
    };

    enum class AlertState {
        Inactive,
        Pending,
        Active,
        Cooldown
    };

    // same fields as the alert_rules sqlite table
    struct AlertRule {
        int64_t     id{ 0 };
        std::string name;
        std::string metric;

        RuleType    rule_type{ RuleType::Threshold };
        Operator    op{ Operator::Gt };
        double      value{ 0.0 };

        int         duration_readings{ 0 };   // sustained threshold only
        double      change_percent{ 0.0 };    // rate of change only
        int         window_readings{ 0 };     // rate of change only

        int         cooldown_seconds{ 300 };
        bool        enabled{ true };
        int64_t     created_at{ 0 };
    };

    // in memory only, tracks state per rule
    struct AlertRuntimeState {
        int64_t    rule_id{ 0 };
        AlertState state{ AlertState::Inactive };
        int64_t    state_entered_at_ms{ 0 };
        int        consecutive_count{ 0 };
        double     peak_value{ 0.0 };
    };

}