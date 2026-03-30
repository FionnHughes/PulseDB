#pragma once
#include <sqlite3.h>

#include "storage/Types.h"

namespace pulsedb {
    class StorageEngine;

    class Downsampler {
    public:
        struct Stats {
            double min, max, mean, p95;
        };

        Downsampler(StorageEngine& engine, sqlite3* db);

        void run_1min(int64_t now_ms);
        void run_1hr(int64_t now_ms);

    private:
        StorageEngine& m_engine;
        sqlite3* m_db;

        Stats compute_stats(const std::vector<MetricReading>&);

        bool write_1min(const std::string& metric, int64_t bucket_ts, const Stats&);
        bool write_1hr(const std::string& metric, int64_t bucket_ts, const Stats&);


    };
}