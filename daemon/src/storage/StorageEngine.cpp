#include <filesystem>
#include <sqlite3.h>
#include <chrono>

#include "StorageEngine.h"

namespace pulsedb {


	StorageEngine::StorageEngine(const std::string& data_dir)
		: m_data_dir(data_dir) {
	}

	bool StorageEngine::open() {
		std::filesystem::path db_path = m_data_dir;
		db_path /= "pulsedb.sqlite";

		int rc = sqlite3_open(db_path.string().c_str(), &m_db);
		if (rc != SQLITE_OK) {
			return false;
		}

		const char* sql = R"(
			CREATE TABLE IF NOT EXISTS metric_summaries_1min (
				id        INTEGER PRIMARY KEY AUTOINCREMENT,
				metric    TEXT NOT NULL,
				bucket_ts INTEGER NOT NULL,
				min_val   REAL NOT NULL,
				max_val   REAL NOT NULL,
				mean_val  REAL NOT NULL,
				p95_val   REAL NOT NULL
			);
			CREATE INDEX IF NOT EXISTS idx_summaries_1min_metric_ts ON metric_summaries_1min(metric, bucket_ts);

			CREATE TABLE IF NOT EXISTS metric_summaries_1hr (
				id        INTEGER PRIMARY KEY AUTOINCREMENT,
				metric    TEXT NOT NULL,
				bucket_ts INTEGER NOT NULL,
				min_val   REAL NOT NULL,
				max_val   REAL NOT NULL,
				mean_val  REAL NOT NULL,
				p95_val   REAL NOT NULL
			);
			CREATE INDEX IF NOT EXISTS idx_summaries_1hr_metric_ts ON metric_summaries_1hr(metric, bucket_ts);
		)";
		char* errmsg = nullptr;
		rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errmsg);
		if (rc != SQLITE_OK) {
			sqlite3_free(errmsg);
			return false;
		}

		m_downsampler = std::make_unique<Downsampler>(*this, m_db);

		m_downsample_timer = std::make_unique<boost::asio::steady_timer>(m_ioc);

		m_downsample_timer->expires_after(std::chrono::seconds(60));
		m_downsample_timer->async_wait([this](const boost::system::error_code& ec) {
			if (!ec) run_downsample();
			});

		m_ioc_thread = std::thread([this]() { m_ioc.run(); });

		return true;
	}

	void StorageEngine::close() {
		if (m_downsample_timer) m_downsample_timer->cancel();
		m_ioc.stop();
		if (m_ioc_thread.joinable()) m_ioc_thread.join();

		if (m_db) {
			sqlite3_close(m_db);
			m_db = nullptr;
		}
	}

	void StorageEngine::run_downsample() {
		int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()
		).count();

		m_downsampler->run_1min(now);

		if (now % 3600000 < 60000)
			m_downsampler->run_1hr(now);

		m_downsample_timer->expires_after(std::chrono::seconds(60));
		m_downsample_timer->async_wait([this](const boost::system::error_code& ec) {
			if (!ec) run_downsample();
			});
	}

	std::vector<std::string> StorageEngine::get_active_metrics() const {
		std::vector<std::string> names;
		names.reserve(m_writers.size());
		for (const auto& [key, _] : m_writers)
			names.push_back(key);
		return names;
	}

	std::string StorageEngine::ts_to_date_string(int64_t day_ts) {
		time_t seconds = day_ts / 1000;
		std::tm tm{};
		gmtime_s(&tm, &seconds);

		char buf[16];
		std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);

		return buf;
	}

	std::string StorageEngine::build_file_path(const std::string& metric, int64_t day_ts) {
		std::filesystem::path p = m_data_dir;
		return (p / metric / (ts_to_date_string(day_ts) + ".pulse")).string();
	}

	std::vector<MetricReading> StorageEngine::query(const std::string& metric, int64_t from_ms, int64_t to_ms) {
		int64_t day_ts = (from_ms / 86400000) * 86400000;
		std::vector<MetricReading> results;
		while (day_ts <= to_ms) {
			std::string filepath = build_file_path(metric, day_ts);
			if (std::filesystem::exists(filepath)) {
				PulseFileReader reader(filepath);
				reader.open();
				auto chunk = reader.query(from_ms, to_ms);
				results.insert(results.end(), chunk.begin(), chunk.end());
				reader.close();
			}
			day_ts = day_ts + 86400000;
		}
		return results;
	}
}