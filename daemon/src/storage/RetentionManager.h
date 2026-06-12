#pragma once
#include <sqlite3.h>
#include <string>

namespace pulsedb {

    struct RetentionConfig {
        int raw_days = 7;
        int min_days = 30;
        int hr_days = 365;
    };

    class RetentionManager {
    public:
        RetentionManager(const std::string& data_dir, sqlite3* db, RetentionConfig config);

        void run(int64_t now_ms);

    private:
        bool delete_pulse_files(int64_t cutoff_ms);
        bool delete_1min_rows(int64_t cutoff_ms);
        bool delete_1hr_rows(int64_t cutoff_ms);

        int64_t date_string_to_ms(const std::string&);

        std::string m_data_dir;
        sqlite3* m_db;
        RetentionConfig m_config;
    };
}