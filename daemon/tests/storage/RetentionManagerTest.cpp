#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sqlite3.h>

#include "storage/RetentionManager.h"

namespace pulsedb {

	class RetentionManagerTest : public ::testing::Test {
	protected:
		void SetUp() override {
			std::filesystem::create_directories(test_dir);
			// in-memory db so nothing to clean up
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
		}

		void TearDown() override {
			sqlite3_close(m_db);
			std::filesystem::remove_all(test_dir);
		}

		sqlite3* m_db = nullptr;
		std::string test_dir = "retention_test_data";

	};

	// creates a very old .pulse file then runs retention and file should be deleted
	TEST_F(RetentionManagerTest, DeletesPulseFilesOlderThanCutoff) {
		std::ofstream(test_dir + "/2020-01-01.pulse").close();
		ASSERT_TRUE(std::filesystem::exists(test_dir + "/2020-01-01.pulse"));

		int64_t now_ms = 1700000000000LL;
		RetentionConfig config;
		RetentionManager rm(test_dir, m_db, config);
		rm.run(now_ms);

		EXPECT_FALSE(std::filesystem::exists(test_dir + "/2020-01-01.pulse"));
	}

	// inserts a very old summary row then runs retention and row should be gone
	TEST_F(RetentionManagerTest, Deletes1MinRowsOlderThanCutoff) {
		const char* insert_sql = "INSERT INTO metric_summaries_1min (metric, bucket_ts, min_val, max_val, mean_val, p95_val) VALUES ('cpu_total', 1000000, 1.0, 2.0, 1.5, 1.9)";
		sqlite3_exec(m_db, insert_sql, nullptr, nullptr, nullptr);

		int64_t now_ms = 1700000000000LL;
		RetentionConfig config;
		RetentionManager rm(test_dir, m_db, config);
		rm.run(now_ms);

		sqlite3_stmt* stmt = nullptr;
		sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM metric_summaries_1min", -1, &stmt, nullptr);
		sqlite3_step(stmt);
		int count = sqlite3_column_int(stmt, 0);
		sqlite3_finalize(stmt);

		EXPECT_EQ(count, 0);
	}

	// same thing for the 1 hr table
	TEST_F(RetentionManagerTest, Deletes1HrRowsOlderThanCutoff) {
		const char* insert_sql = "INSERT INTO metric_summaries_1hr (metric, bucket_ts, min_val, max_val, mean_val, p95_val) VALUES ('cpu_total', 1000000, 1.0, 2.0, 1.5, 1.9)";
		sqlite3_exec(m_db, insert_sql, nullptr, nullptr, nullptr);

		int64_t now_ms = 1700000000000LL;
		RetentionConfig config;
		RetentionManager rm(test_dir, m_db, config);
		rm.run(now_ms);

		sqlite3_stmt* stmt = nullptr;
		sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM metric_summaries_1hr", -1, &stmt, nullptr);
		sqlite3_step(stmt);
		int count = sqlite3_column_int(stmt, 0);
		sqlite3_finalize(stmt);

		EXPECT_EQ(count, 0);
	}
}