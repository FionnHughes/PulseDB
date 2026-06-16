#include <filesystem>
#include <sqlite3.h>
#include <chrono>

#include "StorageEngine.h"
#include "WalManager.h"

namespace pulsedb {

	// just stores the data directory path, open() does real setup
	StorageEngine::StorageEngine(const std::string& data_dir)
		: m_data_dir(data_dir) {
	}

	// creates the sqlite db if needed, replays any leftover WAL files from crashes, then starts the downsampler timer
	bool StorageEngine::open() {
		std::filesystem::path db_path = m_data_dir;

		// sqlite database holds the 1-minute and 1-hour aggregate summaries
		db_path /= "pulsedb.sqlite";

		int rc = sqlite3_open(db_path.string().c_str(), &m_db);
		if (rc != SQLITE_OK) {
			return false;
		}

		// create the summary tables and indexes if they don't already exist
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

		if (std::filesystem::exists(m_data_dir)) {
			std::error_code ec;

			// on startup replay any .wal files left over from a previous crash
			for (const auto& entry : std::filesystem::directory_iterator(m_data_dir, ec)) {
				if (!ec && entry.path().extension() == ".wal") {
					WalManager wal(entry.path());
					wal.replay();
				}
			}
		}
		m_downsampler = std::make_unique<Downsampler>(*this, m_db);

		m_retention = std::make_unique<RetentionManager>(m_data_dir, m_db, RetentionConfig{});

		m_downsample_timer = std::make_unique<boost::asio::steady_timer>(m_ioc);

		// downsampler runs every 60 seconds and it reschedules itself at the end of each run
		m_downsample_timer->expires_after(std::chrono::seconds(60));
		m_downsample_timer->async_wait([this](const boost::system::error_code& ec) {
			if (!ec) run_downsample();
			});

		m_ioc_thread = std::thread([this]() { m_ioc.run(); });

		return true;
	}

	// cancels the timer, stops the writer thread (draining the queue first), then flushes and closes all open writers
	void StorageEngine::close() {
		if (m_downsample_timer) m_downsample_timer->cancel();
		m_ioc.stop();
		if (m_ioc_thread.joinable()) m_ioc_thread.join();

		stop_writer();

		{
			std::lock_guard<std::mutex> lock(m_writers_mutex);
			for (auto& [name, writer] : m_writers) {
				writer->flush();
				writer->close();
			}
			m_writers.clear();
		}

		if (m_db) {
			sqlite3_close(m_db);
			m_db = nullptr;
		}
	}

	// called every 60 seconds and does 1-min aggregation every run, 1-hr only on the hour, retention only once per day
	void StorageEngine::run_downsample() {
		int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()
		).count();

		m_downsampler->run_1min(now);

		// integer division floors to the hour boundary
		int64_t current_hr_bucket = (now / 3600000) * 3600000;

		if (current_hr_bucket != m_last_hr_bucket) {
			m_last_hr_bucket = current_hr_bucket;
			m_downsampler->run_1hr(now);
		}

		std::string today = ts_to_date_string((now / 86400000) * 86400000);
		if (today != m_last_retention_date) {
			m_last_retention_date = today;
			m_retention->run(now);
		}

		m_downsample_timer->expires_after(std::chrono::seconds(60));
		m_downsample_timer->async_wait([this](const boost::system::error_code& ec) {
			if (!ec) run_downsample();
			});
	}

	std::vector<std::string> StorageEngine::get_active_metrics() const {
		std::lock_guard<std::mutex> lock(m_writers_mutex);
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

	// queries across .pulse files should be one file per day, so a multi-day range reads multiple files
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

	// routes a reading to the right PulseFileWriter, creating one if needed and also handles midnight rollover
	bool StorageEngine::append(const std::string& metric, MetricType type, const MetricReading& reading) {
		int64_t day_ts = (reading.timestamp_ms / 86400000) * 86400000;

		std::lock_guard<std::mutex> lock(m_writers_mutex);
		auto it = m_writers.find(metric);
		// if the writer exists but it's for a different day, close it and a fresh one gets created
		if (it != m_writers.end() && it->second->day_start_ts() != day_ts) {
			it->second->flush();
			it->second->close();
			m_writers.erase(it);
			it = m_writers.end();
		}
		if (it == m_writers.end()) {
			std::string filepath = build_file_path(metric, day_ts);
			std::filesystem::create_directories(std::filesystem::path(filepath).parent_path());

			// each metric gets its own WAL file so closing one doesn't affect the others
			std::string wal_path = (std::filesystem::path(m_data_dir) / (metric + ".wal")).string();

			auto writer = std::make_unique<PulseFileWriter>(filepath, type, metric, wal_path);
			if (!writer->open()) return false;

			auto [inserted, ok] = m_writers.emplace(metric, std::move(writer));
			it = inserted;
		}

		return it->second->append(reading);
	}

	void StorageEngine::start_writer(SpscQueue<MetricSnapshot, 1024>& queue) {
		m_writer_running = true;
		m_writer_thread = std::thread([this, &queue]() { writer_loop(queue); });
	}

	void StorageEngine::stop_writer() {
		m_writer_running = false;
		if (m_writer_thread.joinable()) m_writer_thread.join();
	}

	// background thread, it pops snapshots from the queue and writes them to disk
	void StorageEngine::writer_loop(SpscQueue<MetricSnapshot, 1024>& queue) {
		while (m_writer_running) {
			// drain loop after the main loop exits to ensure no snapshots are silently lost on shutdown
			auto result = queue.try_pop();
			if (result.has_value()) {
				write_snapshot(result.value());
			}
			else {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
		while (auto result = queue.try_pop()) {
			write_snapshot(*result);
		}
	}

	// splits a snapshot into individual metric readings and appends each one
	void StorageEngine::write_snapshot(const MetricSnapshot& snap) {
		int64_t ts = snap.timestamp_ms;

		append("cpu_total", MetricType::cpu_total, { ts, snap.cpu_total_percent });

		for (int i = 0; i < (int)snap.cpu_per_core_percent.size(); i++) {
			append("cpu_core_" + std::to_string(i), MetricType::cpu_core, { ts, snap.cpu_per_core_percent[i] });
		}

		append("ram_used", MetricType::ram_used, { ts, (double)snap.ram_used_bytes });
		append("ram_available", MetricType::ram_available, { ts, (double)snap.ram_available_bytes });

		for (int i = 0; i < (int)snap.disks.size(); i++) {
			const auto& d = snap.disks[i];
			append("disk_" + std::to_string(i) + "_read", MetricType::disk_read, { ts, (double)d.read_bytes_per_sec });
			append("disk_" + std::to_string(i) + "_write", MetricType::disk_write, { ts, (double)d.write_bytes_per_sec });
		}

		for (int i = 0; i < (int)snap.network_adapters.size(); i++) {
			const auto& n = snap.network_adapters[i];
			append("net_" + std::to_string(i) + "_in", MetricType::net_in, { ts, (double)n.bytes_in_per_sec });
			append("net_" + std::to_string(i) + "_out", MetricType::net_out, { ts, (double)n.bytes_out_per_sec });
		}
	}
}