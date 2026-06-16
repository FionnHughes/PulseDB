#include <cmath>
#include <limits>

#include "Downsampler.h"
#include "StorageEngine.h"


namespace pulsedb {
	Downsampler::Downsampler(StorageEngine& engine, sqlite3* db) : m_engine(engine), m_db(db) { }

	// calculates min, max, mean, and p95 over a list of readings
	Downsampler::Stats Downsampler::compute_stats(const std::vector<MetricReading>& readings) {
		Downsampler::Stats stats{};
		if (readings.empty()) return stats;

		std::vector<double> values;
		values.reserve(readings.size());
		for (const auto& r : readings) {
			values.push_back(r.value);
			stats.mean += r.value;
		}
		
		// need sorted values for the percentile calculation
		std::sort(values.begin(), values.end());

		stats.min = values.front();
		stats.max = values.back();
		stats.mean /= readings.size();
		
		// nearest-rank p95 formula [ ceil(0.95 * n) - 1 ] gives the correct index
		size_t p95_idx = static_cast<size_t>(std::ceil(0.95 * static_cast<double>(values.size()))) - 1;
		stats.p95 = values[p95_idx];

		return stats;
	}

	bool Downsampler::write_1min(const std::string& metric, int64_t bucket_ts, const Downsampler::Stats& stats) {
		const char* sql = "INSERT INTO metric_summaries_1min (metric, bucket_ts, min_val, max_val, mean_val, p95_val) VALUES (?, ?, ?, ?, ?, ?)";

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
			return false;

		sqlite3_bind_text(stmt, 1, metric.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt, 2, bucket_ts);
		sqlite3_bind_double(stmt, 3, stats.min);
		sqlite3_bind_double(stmt, 4, stats.max);
		sqlite3_bind_double(stmt, 5, stats.mean);
		sqlite3_bind_double(stmt, 6, stats.p95);

		int rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);

		return rc == SQLITE_DONE;
	}

	bool Downsampler::write_1hr(const std::string& metric, int64_t bucket_ts, const Downsampler::Stats& stats) {
		const char* sql = "INSERT INTO metric_summaries_1hr (metric, bucket_ts, min_val, max_val, mean_val, p95_val) VALUES (?, ?, ?, ?, ?, ?)";

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
			return false;

		sqlite3_bind_text(stmt, 1, metric.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt, 2, bucket_ts);
		sqlite3_bind_double(stmt, 3, stats.min);
		sqlite3_bind_double(stmt, 4, stats.max);
		sqlite3_bind_double(stmt, 5, stats.mean);
		sqlite3_bind_double(stmt, 6, stats.p95);

		int rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);

		return rc == SQLITE_DONE;
	}

	// queries the last 60 seconds of raw readings for each active metric, computes stats, writes to metric_summaries_1min
	void Downsampler::run_1min(int64_t now_ms) {
		int64_t from_ms = now_ms - 60000;
		int64_t to_ms = now_ms;
		int64_t bucket_ts = (now_ms / 60000) * 60000;

		std::vector<std::string> metrics = m_engine.get_active_metrics();
		for (auto& metric : metrics) {
			std::vector<MetricReading> readings = m_engine.query(metric, from_ms, to_ms);

			if (readings.empty()) continue;
			Stats stat = compute_stats(readings);
			if (!write_1min(metric, bucket_ts, stat)) {
				// log failure
			}
		}

	}

	// queries the last hour of 1 min rows from sqlite and combines them into an hourly row per metric
	void Downsampler::run_1hr(int64_t now_ms) {
		int64_t from_ts = now_ms - 3600000;
		int64_t to_ts = now_ms;
		int64_t bucket_ts = (now_ms / 3600000) * 3600000;

		const char* metrics_sql = "SELECT DISTINCT metric FROM metric_summaries_1min WHERE bucket_ts >= ? AND bucket_ts < ?";
		sqlite3_stmt* metrics_stmt = nullptr;
		sqlite3_prepare_v2(m_db, metrics_sql, -1, &metrics_stmt, nullptr);
		sqlite3_bind_int64(metrics_stmt, 1, from_ts);
		sqlite3_bind_int64(metrics_stmt, 2, to_ts);

		while (sqlite3_step(metrics_stmt) == SQLITE_ROW) {
			std::string metric = reinterpret_cast<const char*>(sqlite3_column_text(metrics_stmt, 0));

			Stats stat{};
			// init min to the largest possible value so any real reading will replace it
			stat.min = std::numeric_limits<double>::max();
			// init max to the smallest possible value so any real reading will replace it
			stat.max = std::numeric_limits<double>::lowest();
			int row_count = 0;

			const char* sql = "SELECT min_val, max_val, mean_val, p95_val FROM metric_summaries_1min WHERE metric = ? AND bucket_ts >= ? AND bucket_ts < ?";

			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) continue;

			sqlite3_bind_text(stmt, 1, metric.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt, 2, from_ts);
			sqlite3_bind_int64(stmt, 3, to_ts);

			while (sqlite3_step(stmt) == SQLITE_ROW) {
				double min = sqlite3_column_double(stmt, 0);
				double max = sqlite3_column_double(stmt, 1);
				double mean = sqlite3_column_double(stmt, 2);
				double p95 = sqlite3_column_double(stmt, 3);

				if (max > stat.max) stat.max = max;
				if (min < stat.min) stat.min = min;
				stat.mean += mean;
				stat.p95 += p95;
				row_count++;
			}
			sqlite3_finalize(stmt);
			if (row_count == 0) continue;

			stat.mean /= row_count;
			stat.p95 /= row_count;

			write_1hr(metric, bucket_ts, stat);
		}
		sqlite3_finalize(metrics_stmt);
	}
}
