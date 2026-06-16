#pragma once
#include <sqlite3.h>

#include "storage/Types.h"

namespace pulsedb {
    class StorageEngine;

    // reads raw 1 second data from .pulse files and writes aggregated summaries into sqlite
    class Downsampler {
    public:
        // aggregated stats for a time bucket
        struct Stats {
            double min, max, mean, p95;
        };

        Downsampler(StorageEngine& engine, sqlite3* db);

        // called every minute to query the last 60 seconds of raw readings and write a summary row
        void run_1min(int64_t now_ms);

        // called once per hour to collect the last 60 one minute rows into an hourly row
        void run_1hr(int64_t now_ms);

        Stats compute_stats(const std::vector<MetricReading>&);


    private:
        StorageEngine& m_engine;
        sqlite3* m_db;

        bool write_1min(const std::string& metric, int64_t bucket_ts, const Stats&);
        bool write_1hr(const std::string& metric, int64_t bucket_ts, const Stats&);
    };
}