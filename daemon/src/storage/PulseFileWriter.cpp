#include <iostream>
#include <chrono>

#include "lz4.h"

#include "PulseFileWriter.h"

namespace pulsedb {

	// these assert at compile time to check if a struct size changes since any mismatch would break existing files
	static_assert(sizeof(FileHeader) == 64, "FileHeader size mismatch");
	static_assert(sizeof(ChunkIndexEntry) == 16, "ChunkIndexEntry size mismatch");
	static_assert(sizeof(ChunkHeader) == 16, "ChunkHeader size mismatch");

	// just stores the params open() does the actual file setup
	PulseFileWriter::PulseFileWriter(const std::string& filepath, MetricType type, const std::string& metric_name, const std::filesystem::path& wal_path) : m_wal(wal_path){
		m_filepath = filepath;
		m_metric_type = type;
		m_metric_name = metric_name;
	}
	PulseFileWriter::~PulseFileWriter() {
		flush();
		close();
	}

	// opens the file for appending and reads the existing header if the file is already there, writes a new one if not
	bool PulseFileWriter::open() {
		// checking this first so we know whether to create a new header or resume from an existing file
		bool file_exists = std::filesystem::exists(m_filepath);

		if (file_exists) {
			// read + write mode so we can seek back to update the chunk count in the header after each write
			m_file.open(m_filepath, std::ios::binary | std::ios::in | std::ios::out);
			if (!m_file.is_open()) {
				std::cerr << "Failed to open existing file\n";
				return false;
			}

			FileHeader header{};
			m_file.read(reinterpret_cast<char*>(&header), sizeof(header));
			if (!m_file.good() || std::memcmp(header.magic, "PULS", 4) != 0) {
				std::cerr << "Existing file has bad header\n";
				m_file.close();
				return false;
			}
			m_chunk_count = header.chunk_count;
			m_day_start_ts = header.day_start_ts;

			m_file.seekp(0, std::ios::end);
		}
		else {
			// new file so we truncate to start fresh
			m_file.open(m_filepath, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
			if (!m_file.is_open()) {
				std::cerr << "Failed to open file\n";
				return false;
			}
			write_file_header();
			write_blank_index();
		}

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

	// packs the chunk buffer into a delta-encoded + lz4-compressed blob, logs it to the WAL, then writes it to the .pulse file and updates the index
	bool PulseFileWriter::compress_and_write_chunk() {
		if (m_chunk_buffer.empty()) return false;
		if (!m_file.is_open() || !m_file.good()) return false;

		// all timestamps in this chunk are stored as uint16 deltas from this base
		int64_t base_ts = m_chunk_buffer.front().timestamp_ms;
		uint16_t reading_count = static_cast<uint16_t>(m_chunk_buffer.size());

		// 10 bytes per reading: 2 for the uint16 timestamp delta + 8 for the double value
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

		// LZ4 needs a worst-case output buffer size before it can compress
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

		// WAL entry written first so that if we crash after this but before the pulse write, replay will recover the chunk
		if (!m_wal.append(m_filepath, compressed_bytes)) return false;


		uint32_t chunk_byte_offset = static_cast<uint32_t>(m_file.tellp());
		m_file.write(reinterpret_cast<const char*>(compressed.data()), compressed_size);

		if (!m_file.good()) return false;

		ChunkIndexEntry chunk_index_entry{};
		chunk_index_entry.chunk_start_ts = base_ts;
		chunk_index_entry.byte_offset = chunk_byte_offset;
		chunk_index_entry.compressed_size = static_cast<uint32_t>(compressed_size);

		// seek to this chunk's slot in the pre allocated index
		m_file.seekp(sizeof(FileHeader) + (sizeof(ChunkIndexEntry) * m_chunk_count), std::ios::beg);
		m_file.write(reinterpret_cast<const char*>(&chunk_index_entry), sizeof(chunk_index_entry));

		uint32_t new_chunk_count = m_chunk_count + 1;
		// update the chunk count field in the file header
		m_file.seekp(offsetof(FileHeader, chunk_count), std::ios::beg);
		m_file.write(reinterpret_cast<const char*>(&new_chunk_count), sizeof(new_chunk_count));
		if (!m_file.good()) return false;
		
		m_file.flush();
		m_chunk_count = new_chunk_count;
		m_chunk_buffer.clear();

		// now that the chunk is safely on disk, WAL entry is no longer needed
		m_wal.clear();

		m_file.seekp(0, std::ios::end);
		return true;
	}

	// pre-allocates all 1440 index slots so writes can seek directly to the right slot without appending
	void PulseFileWriter::write_blank_index() {
		//chunks now begin at byte 23,104 (DATA_START_OFFSET)
		std::vector<uint8_t> blank(MAX_CHUNKS_PER_DAY * INDEX_ENTRY_SIZE, 0x00);
		m_file.write(reinterpret_cast<const char*>(blank.data()), blank.size());
		m_file.flush();
	}

	// writes the 64 byte file header and sets magic, version, metric info, and m_day_start_ts which the engine uses for rollover detection
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

		m_day_start_ts = header.day_start_ts;

		m_file.write(reinterpret_cast<const char*>(&header), sizeof(header));
	}
}

