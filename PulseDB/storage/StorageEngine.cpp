//#include <iostream>
#include <filesystem>

#include "StorageEngine.h"

pulsedb::StorageEngine::StorageEngine(const std::string& data_dir)
	: m_data_dir(data_dir) {
}

std::string pulsedb::StorageEngine::ts_to_date_string(int64_t day_ts) {
	time_t seconds = day_ts / 1000;
	std::tm* tm = std::gmtime(&seconds);

	char buf[16];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm);

	return buf;
}

std::string pulsedb::StorageEngine::build_file_path(const std::string& metric, int64_t day_ts) {
	std::filesystem::path p = m_data_dir;
	return (p / metric / (ts_to_date_string(day_ts) + ".pulse")).string();
}

std::vector<pulsedb::MetricReading> pulsedb::StorageEngine::query(const std::string& metric, int64_t from_ms, int64_t to_ms) {
	int64_t day_ts = (from_ms / 86400000) * 86400000;
	std::vector<pulsedb::MetricReading> results;
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