#include <filesystem>
#include <cstdio>
#include <ctime>

#include "RetentionManager.h"

namespace pulsedb {
	RetentionManager::RetentionManager(const std::string& data_dir, sqlite3* db, RetentionConfig config) : m_data_dir(data_dir), m_db(db), m_config(config) {}

	// parses "YYYY-MM-DD" from a .pulse filename stem and returns midnight UTC in unix milliseconds
	int64_t RetentionManager::date_string_to_ms(const std::string& stem) {
		int year = 0, month = 0, day = 0;
		if (std::sscanf(stem.c_str(), "%d-%d-%d", &year, &month, &day) != 3) {
			return -1;
		}

		std::tm t{};
		t.tm_year = year - 1900;
		t.tm_mon = month - 1;
		t.tm_mday = day;

		std::time_t seconds = _mkgmtime(&t);
		if (seconds == -1) return -1;

		return static_cast<int64_t>(seconds) * 1000LL;
	}

	// calculates cutoff timestamps from config then runs the deletion passes
	void RetentionManager::run(int64_t now_ms) {
		int64_t raw_cutoff = now_ms - (m_config.raw_days * 86400000LL);
		int64_t min_cutoff = now_ms - (m_config.min_days * 86400000LL);
		int64_t hr_cutoff = now_ms - (m_config.hr_days * 86400000LL);

		delete_pulse_files(raw_cutoff);
		delete_1min_rows(min_cutoff);
		delete_1hr_rows(hr_cutoff);
	}

	// walks the data directory looking for.pulse files older than the cutoff and removes them
	bool RetentionManager::delete_pulse_files(int64_t cutoff_ms) {
		bool success = true;
		for (const std::filesystem::directory_entry& file : std::filesystem::recursive_directory_iterator(m_data_dir)) {
			if (file.is_regular_file() && file.path().extension() == ".pulse") {
				int64_t file_ts = date_string_to_ms(file.path().stem().string());
				if (file_ts != -1 && file_ts < cutoff_ms) {
					try {
						std::filesystem::remove(file.path());
					}
					catch (const std::filesystem::filesystem_error&) {
						success = false;
					}
				}
			}
		}
		return success;
	}

	// deletes rows from metric_summaries_1min older than the cutoff
	bool RetentionManager::delete_1min_rows(int64_t cutoff_ms) {
		const char* metrics_sql = "DELETE FROM metric_summaries_1min WHERE bucket_ts < ?";
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db, metrics_sql, -1, &stmt, nullptr) != SQLITE_OK) {
			return false;
		}
		sqlite3_bind_int64(stmt, 1, cutoff_ms);
		int rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);

		return rc == SQLITE_DONE;
	}

	// deletes rows from metric_summaries_1hr older than the cutoff
	bool RetentionManager::delete_1hr_rows(int64_t cutoff_ms) {
		const char* metrics_sql = "DELETE FROM metric_summaries_1hr WHERE bucket_ts < ?";
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db, metrics_sql, -1, &stmt, nullptr) != SQLITE_OK) {
			return false;
		}
		sqlite3_bind_int64(stmt, 1, cutoff_ms);
		int rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);

		return rc == SQLITE_DONE;
	}
}
