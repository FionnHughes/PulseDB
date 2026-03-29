#include <iostream>
#include <chrono>

#include "PulseFileReader.h"

pulsedb::PulseFileReader::PulseFileReader(const std::string& filepath) {
	m_filepath = filepath;
}

bool pulsedb::PulseFileReader::open() {
	m_file.open(m_filepath, std::ios::binary | std::ios::in);

	if (!m_file.is_open()) {
		std::cerr << "Failed to open file\n";
		return false;
	}
	m_file.read(reinterpret_cast<char*>(&m_header), sizeof(m_header));
	if (!m_file.good()) return false;
	
	if (memcmp(m_header.magic, PULSE_MAGIC, 4) != 0) return false;
	if (m_header.version != PULSE_VERSION) return false;

	m_file.seekg(m_header.chunk_index_offset, std::ios::beg);
	m_index.resize(m_header.chunk_count);

	m_file.read(reinterpret_cast<char*>(m_index.data()), sizeof(ChunkIndexEntry) * m_header.chunk_count);
	if (!m_file.good()) return false;

	return true;
}

void pulsedb::PulseFileReader::close() {
	m_file.close();
}

std::vector<pulsedb::MetricReading> pulsedb::PulseFileReader::query(int64_t from_ms, int64_t to_ms) {
	std::vector<pulsedb::MetricReading> results;
	results.reserve(m_header.chunk_count * m_header.readings_per_chunk);

	for (uint32_t i = 0; i < m_header.chunk_count; i++){
		const auto& entry = m_index[i];

		if (entry.byte_offset == 0) continue;

		int64_t chunk_end = entry.chunk_start_ts + (static_cast<int64_t>(m_header.readings_per_chunk) * m_header.collection_interval_ms);
		if (chunk_end >= from_ms && entry.chunk_start_ts <= to_ms) {
			std::vector<char> compressed_buf(entry.compressed_size);
			
			m_file.seekg(entry.byte_offset, std::ios::beg);
			m_file.read(compressed_buf.data(), entry.compressed_size);
			if (!m_file.good()) continue;

			const char* ptr = compressed_buf.data();

			ChunkHeader chunk_header;
			memcpy(&chunk_header, ptr, sizeof(ChunkHeader));
			ptr += sizeof(ChunkHeader);

			std::vector<pulsedb::MetricReading> readings;
			readings.reserve(chunk_header.reading_count);

			for (uint16_t j = 0; j < chunk_header.reading_count; j++) {
				uint16_t delta;
				memcpy(&delta, ptr, sizeof(delta));
				ptr += sizeof(delta);

				MetricReading r;
				r.timestamp_ms = chunk_header.base_timestamp_ms + delta;
				memcpy(&r.value, ptr, sizeof(r.value));
				ptr += sizeof(r.value);

				readings.push_back(r);
			}

			for (auto& reading : readings) {
				if (reading.timestamp_ms >= from_ms && reading.timestamp_ms <= to_ms) {
					results.push_back(reading);
				}
			}
		}
	}
	return results;
}

