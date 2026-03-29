#pragma once
#include <string>
#include <vector>
#include <unordered_map>

#include <sqlite3.h>
#include <boost/asio.hpp>

#include "storage/Types.h"
#include "storage/PulseFileWriter.h"
#include "storage/PulseFileReader.h"


namespace pulsedb {
    class StorageEngine {
    public:
        StorageEngine(const std::string& data_dir);

        bool open();
        void close();

        std::vector<MetricReading> query(const std::string& metric, int64_t from_ms, int64_t to_ms);

        bool append(const std::string& metric, MetricType type, const MetricReading& reading);
        void flush_all();

    private:
        std::string m_data_dir;
        std::unordered_map<std::string, PulseFileWriter> m_writers;
        sqlite3* m_db = nullptr;
        boost::asio::io_context m_ioc;
        std::unique_ptr<boost::asio::steady_timer> m_downsample_timer;

        // helpers
        std::string build_file_path(const std::string& metric, int64_t day_ts);
        std::string ts_to_date_string(int64_t day_ts);
        void handle_midnight_rollover();
        void run_downsample();
    };
}