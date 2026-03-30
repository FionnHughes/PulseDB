#include <iostream>
#include <chrono>

#include "lz4.h"

#include "PulseFileWriter.h"

namespace pulsedb {

	static_assert(sizeof(FileHeader) == 64, "FileHeader size mismatch");
	static_assert(sizeof(ChunkIndexEntry) == 16, "ChunkIndexEntry size mismatch");
	static_assert(sizeof(ChunkHeader) == 16, "ChunkHeader size mismatch");

	PulseFileWriter::PulseFileWriter(const std::string& filepath, MetricType type, const std::string& metric_name, const std::filesystem::path& wal_path) {
		m_filepath = filepath;
		m_metric_type = type;
		m_metric_name = metric_name;
		m_wal = wal_path;
	}
	PulseFileWriter::~PulseFileWriter() {
		flush();
		close();
	}

	bool PulseFileWriter::open() {
		m_file.open(m_filepath, std::ios::binary | std::ios::out | std::ios::trunc);

		if (!m_file.is_open()) {
			std::cerr << "Failed to open file\n";
			return false;
		}

		write_file_header();
		write_blank_index();
		return true;
	}

	void PulseFileWriter::close() {
		m_file.close();
	}

	bool PulseFileWriter::append(const MetricReading& reading) {
		if (!m_file.is_open() || !m_file.good()) return false;
		if (m_chunk_buffer.size() >= CHUNK_SIZE) {
			if (!compress_and_write_chunk()) return false;
		}
		m_chunk_buffer.push_back(reading);
		return true;
	}

	bool PulseFileWriter::flush() {
		if (m_chunk_buffer.empty()) return true;
		if (!m_file.is_open() || !m_file.good()) return false;
		if (!compress_and_write_chunk()) return false;
		return true;
	}


	bool PulseFileWriter::compress_and_write_chunk() {
		if (m_chunk_buffer.empty()) return false;
		if (!m_file.is_open() || !m_file.good()) return false;

		int64_t base_ts = m_chunk_buffer.front().timestamp_ms;
		uint16_t reading_count = static_cast<uint16_t>(m_chunk_buffer.size());
		uint32_t uncompressed_size = sizeof(ChunkHeader) + (reading_count * 10);

		std::vector<uint8_t> uncompressed(uncompressed_size);

		ChunkHeader chunk_header{};
		chunk_header.base_timestamp_ms = base_ts;
		chunk_header.reading_count = reading_count;
		chunk_header.reserved = 0x0000;
		chunk_header.uncompressed_size = uncompressed_size;
		std::memcpy(uncompressed.data(), &chunk_header, sizeof(ChunkHeader));

		size_t offset = sizeof(ChunkHeader);
		for (const auto& reading : m_chunk_buffer) {
			uint16_t delta = static_cast<uint16_t>(reading.timestamp_ms - base_ts);
			std::memcpy(uncompressed.data() + offset, &delta, sizeof(delta));
			offset += sizeof(delta);
			std::memcpy(uncompressed.data() + offset, &reading.value, sizeof(reading.value));
			offset += sizeof(reading.value);
		}
		int max_compressed_size = LZ4_compressBound(uncompressed_size);
		std::vector<char> compressed(max_compressed_size);

		int compressed_size = LZ4_compress_default(
			reinterpret_cast<const char*>(uncompressed.data()),
			compressed.data(),
			uncompressed_size,
			max_compressed_size
		);

		if (compressed_size <= 0) return false;

		std::vector<uint8_t> compressed_bytes(compressed.begin(),
			compressed.begin() + compressed_size);
		m_wal.append(m_filepath, compressed_bytes);


		uint32_t chunk_byte_offset = static_cast<uint32_t>(m_file.tellp());
		m_file.write(reinterpret_cast<const char*>(compressed.data()), compressed_size);

		if (!m_file.good()) return false;

		ChunkIndexEntry chunk_index_entry{};
		chunk_index_entry.chunk_start_ts = base_ts;
		chunk_index_entry.byte_offset = chunk_byte_offset;
		chunk_index_entry.compressed_size = static_cast<uint32_t>(compressed_size);
		m_file.seekp(sizeof(FileHeader) + (sizeof(ChunkIndexEntry) * m_chunk_count), std::ios::beg);
		m_file.write(reinterpret_cast<const char*>(&chunk_index_entry), sizeof(chunk_index_entry));

		m_file.seekp(offsetof(FileHeader, chunk_count), std::ios::beg);
		m_chunk_count++;
		m_file.write(reinterpret_cast<const char*>(&m_chunk_count), sizeof(m_chunk_count));
		if (!m_file.good()) return false;

		m_file.seekp(0, std::ios::end);
		m_chunk_buffer.clear();

		return true;
	}

	void PulseFileWriter::write_blank_index() {
		//chunks now begin at byte 23,104 (DATA_START_OFFSET)
		std::vector<uint8_t> blank(MAX_CHUNKS_PER_DAY * INDEX_ENTRY_SIZE, 0x00);
		m_file.write(reinterpret_cast<const char*>(blank.data()), blank.size());
		m_file.flush();
	}

	void PulseFileWriter::write_file_header() {
		auto now = std::chrono::system_clock::now();
		int64_t creation_ts = std::chrono::duration_cast<std::chrono::milliseconds>(
			now.time_since_epoch()).count();

		FileHeader header{};
		std::memcpy(header.magic, "PULS", 4);
		header.version = 1;
		header.metric_type_id = static_cast<uint8_t>(m_metric_type);
		header.reserved = 0x00;
		header.creation_ts = creation_ts;
		header.day_start_ts = (creation_ts / 86400000LL) * 86400000LL;
		header.chunk_count = 0;
		header.chunk_index_offset = 64;
		header.readings_per_chunk = CHUNK_SIZE;
		header.collection_interval_ms = 1000;
		m_metric_name.copy(header.metric_name, std::min(m_metric_name.size(), size_t(24)));

		m_file.write(reinterpret_cast<const char*>(&header), sizeof(header));
	}
}

