#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <sqlite3.h>
#include <boost/asio.hpp>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>      // open(), O_RDWR, O_CREAT
#include <sys/file.h>   // flock(), LOCK_EX, LOCK_NB, LOCK_UN
#include <unistd.h>     // close()
#endif

#include "storage/Types.h"
#include "storage/Downsampler.h"
#include "storage/RetentionManager.h"
#include "storage/PulseFileWriter.h"
#include "storage/PulseFileReader.h"
#include "collector/MetricSnapshot.h"
#include "queue/SpscQueue.h"


namespace pulsedb {
    // the main storage layer which owns all the .pulse writers, the downsampler, retention, and the background writer thread
    class StorageEngine {
    public:
        StorageEngine(const std::string& data_dir, RetentionConfig retention_config = RetentionConfig{});

        bool open();
        void close();

        std::vector<MetricReading> query(const std::string& metric, int64_t from_ms, int64_t to_ms);

        // one point per summary row (ts = bucket_ts, value = mean_val), plus aggregated
        // stats across the whole range. resolution must be "1min" or "1hr".
        struct SummaryQueryResult {
            std::vector<MetricReading> points;
            Downsampler::Stats stats{};
            bool has_data = false;
        };
        SummaryQueryResult query_summary(const std::string& metric, int64_t from_ms, int64_t to_ms, const std::string& resolution);

        bool append(const std::string& metric, MetricType type, const MetricReading& reading);
        std::vector<std::string> get_active_metrics() const;

        void start_writer(SpscQueue<MetricSnapshot, 1024>& queue);
        void stop_writer();

    private:
        // protects m_writers as it is accessed from the writer thread and potentially the query path
        mutable std::mutex m_writers_mutex;
        std::string m_data_dir;

        RetentionConfig m_retention_config;

        // one writer per metric, created on the first append for that metric on the current day
        std::unordered_map<std::string, std::unique_ptr<PulseFileWriter>> m_writers;
        sqlite3* m_db = nullptr;

        // os level lock, to prevent a second instance opening in the same directory
        #ifdef _WIN32
            HANDLE m_lock_handle = INVALID_HANDLE_VALUE;
        #else
            int m_lock_fd = -1;
        #endif
        bool acquire_lock();
        void release_lock();

        // drives the downsampler timer it runs on m_ioc_thread separate from the writer thread
        boost::asio::io_context m_ioc;
        std::unique_ptr<boost::asio::steady_timer> m_downsample_timer;
        std::thread m_ioc_thread;
        int64_t m_last_hr_bucket{ -1 };

        std::unique_ptr<Downsampler> m_downsampler;
        std::unique_ptr<RetentionManager> m_retention;
        std::string m_last_retention_date;

        // helpers
        std::string build_file_path(const std::string& metric, int64_t day_ts);
        std::string ts_to_date_string(int64_t day_ts);
        void run_downsample();

        void writer_loop(SpscQueue<MetricSnapshot, 1024>& queue);
        void write_snapshot(const MetricSnapshot& snap);

        std::thread m_writer_thread;

        // signals the writer thread to stop, it drains remaining queue items before it actually exits
        std::atomic<bool> m_writer_running{ false };
    };
}
