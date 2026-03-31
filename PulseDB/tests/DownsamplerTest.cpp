#include <gtest/gtest.h>
#include <filesystem>

#include "storage/StorageEngine.h"
#include "storage/Downsampler.h"

namespace pulsedb {

	class DownsamplerTest : public ::testing::Test {
	protected:
		void SetUp() override {
			std::filesystem::create_directories(test_dir);
			m_engine = std::make_unique<StorageEngine>(test_dir);
			sqlite3_open(":memory:", &m_db);

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
			int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errmsg);
			if (rc != SQLITE_OK) {
				sqlite3_free(errmsg);
				return;
			}
			
			m_downsampler = std::make_unique<Downsampler>(*m_engine, m_db);
		}

		void TearDown() override {
			sqlite3_close(m_db);
			std::filesystem::remove_all(test_dir);
		}
		std::string test_dir = "test_data";
		std::string test_filepath = "test_data/cpu_total/2023-11-14.pulse";

		sqlite3* m_db = nullptr;
		std::unique_ptr<StorageEngine> m_engine;
		std::unique_ptr<Downsampler> m_downsampler;

	};

	TEST_F(DownsamplerTest, ComputeStatsCorrect) {
		std::vector<MetricReading> readings;
		for (int i = 1; i <= 10; i++) {
			readings.push_back({ 0, static_cast<double>(i) });
		}
		Downsampler::Stats stat = m_downsampler->compute_stats(readings);

		EXPECT_DOUBLE_EQ(stat.min, 1.0);
		EXPECT_DOUBLE_EQ(stat.max, 10.0);
		EXPECT_DOUBLE_EQ(stat.mean, 5.5);
		EXPECT_DOUBLE_EQ(stat.p95, 10.0);
	}


	TEST_F(DownsamplerTest, Run1HrAggregatesFrom1MinRows) {
		int64_t now_ms = 1700000000000;

		const char* insert_sql = R"(
			INSERT INTO metric_summaries_1min (metric, bucket_ts, min_val, max_val, mean_val, p95_val)
			VALUES
				('cpu_total', 1699996460000, 1.0, 10.0, 5.0, 8.0),
				('cpu_total', 1699996520000, 2.0, 20.0, 10.0, 16.0),
				('cpu_total', 1699996580000, 3.0, 30.0, 15.0, 24.0);
		)";

		sqlite3_exec(m_db, insert_sql, nullptr, nullptr, nullptr);
		
		m_downsampler->run_1hr(now_ms);

		const char* select_sql = "SELECT min_val, max_val, mean_val, p95_val FROM metric_summaries_1hr WHERE metric = 'cpu_total'";

		sqlite3_stmt* stmt = nullptr;
		sqlite3_prepare_v2(m_db, select_sql, -1, &stmt, nullptr);
		ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);

		EXPECT_DOUBLE_EQ(sqlite3_column_double(stmt, 0), 1.0);
		EXPECT_DOUBLE_EQ(sqlite3_column_double(stmt, 1), 30.0);
		EXPECT_DOUBLE_EQ(sqlite3_column_double(stmt, 2), 10.0);
		EXPECT_DOUBLE_EQ(sqlite3_column_double(stmt, 3), 16.0);

		sqlite3_finalize(stmt);
	}

	TEST_F(DownsamplerTest, EmptyInputSkipsWrite) {
		std::vector<MetricReading> readings;

		Downsampler::Stats stat = m_downsampler->compute_stats(readings);

		EXPECT_DOUBLE_EQ(stat.min, 0);
		EXPECT_DOUBLE_EQ(stat.max, 0);
		EXPECT_DOUBLE_EQ(stat.mean, 0);
		EXPECT_DOUBLE_EQ(stat.p95, 0);
	}
}