// needed to set XXH32_state_t as a complete type so we can stack-allocate it errors otherwise
#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

#include "WalManager.h"

namespace pulsedb{

	// if the WAL doesn't exist, creates it and if it has a bad header, recreates it. scans existing entries to find the highest seq_no
	WalManager::WalManager(const std::filesystem::path& wal_path) : path_(wal_path) {

		if (!std::filesystem::exists(path_)) {
			std::ofstream init(path_, std::ios::binary);
			write_file_header(init);
			init.close();
		}
		{
			std::ifstream probe(path_, std::ios::binary);
			WalFileHeader header{};
			probe.read(reinterpret_cast<char*>(&header), sizeof(header));

			// "WAL\0" in hex
			constexpr uint8_t expected_magic[4] = { 0x57, 0x41, 0x4C, 0x00 };
			if (probe.gcount() < static_cast<std::streamsize>(sizeof(header)) ||
				std::memcmp(header.magic, expected_magic, 4) != 0) {
				probe.close();
				std::error_code ec;
				std::filesystem::remove(path_, ec);
				std::ofstream init(path_, std::ios::binary);
				write_file_header(init);
				init.close();
			}
		}

		// append mode so new entries go at the end without touching existing ones
		out_.open(path_, std::ios::binary | std::ios::app);
		if (!out_.is_open())
			throw std::runtime_error("WAL: failed to open file for writing: " + path_.string());

		std::ifstream in(path_, std::ios::binary);
		WalFileHeader header{};
		in.read(reinterpret_cast<char*>(&header), sizeof(header));

		while (true) {
			WalEntryHeader entry{};
			in.read(reinterpret_cast<char*>(&entry), sizeof(entry));
			if (in.gcount() < static_cast<std::streamsize>(sizeof(entry))) break;

			std::vector<uint8_t> chunk(entry.chunk_data_size);
			in.read(reinterpret_cast<char*>(chunk.data()), entry.chunk_data_size);
			if (in.gcount() < static_cast<std::streamsize>(entry.chunk_data_size)) break;
			
			uint32_t stored_checksum = 0;
			in.read(reinterpret_cast<char*>(&stored_checksum), sizeof(stored_checksum));
			if (in.gcount() < static_cast<std::streamsize>(sizeof(stored_checksum))) break;

			if (compute_checksum(entry, chunk.data(), entry.chunk_data_size) != stored_checksum) break;

			// scan valid entries to find the highest seq_no so new entries continue from the right number
			seq_ = entry.seq_no;
		}
	}

	void WalManager::write_file_header(std::ofstream& file) {
		file.seekp(0, std::ios::beg);
		WalFileHeader header{ {0x57, 0x41, 0x4C, 0x00} };
		file.write(reinterpret_cast<char*>(&header), sizeof(header));
		file.flush();
	}

	// writes entry header, chunk data, then xxhash32 checksum. decrements seq_ and returns false if the write fails
	bool WalManager::append(const std::filesystem::path& target_file, const std::vector<uint8_t>& chunk_data) {
		seq_++;
		WalEntryHeader entry{};
		entry.seq_no = seq_;
		memcpy(entry.target_file_path, target_file.string().c_str(), target_file.string().size());
		entry.chunk_data_size = static_cast<uint32_t>(chunk_data.size());

		out_.write(reinterpret_cast<char*>(&entry), sizeof(entry));
		out_.write(reinterpret_cast<const char*>(chunk_data.data()), entry.chunk_data_size);

		uint32_t checksum = compute_checksum(entry, chunk_data.data(), entry.chunk_data_size);
		out_.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));

		out_.flush();

		if (!out_.good()) {
			seq_--;
			return false;
		}
		return true;
	}

	// called after a successful .pulse write. it deletes the WAL and sentinel, then recreates a fresh WAL with just the header
	void WalManager::clear() {
		if (out_.is_open()) out_.close();
		std::error_code ec;
		std::filesystem::remove(path_, ec);
		std::filesystem::path sentinel = path_;
		sentinel.replace_extension(".applied");
		std::filesystem::remove(sentinel, ec);
		out_.open(path_, std::ios::binary | std::ios::app);
		write_file_header(out_);
	}

	// reads WAL entries, checks the sentinel to skip already-applied ones, and appends chunks to the target .pulse file
	void WalManager::replay() {
		if (!std::filesystem::exists(path_)) return;

		std::ifstream in(path_, std::ios::binary);
		WalFileHeader header{};
		in.read(reinterpret_cast<char*>(&header), sizeof(header));

		constexpr uint8_t expected_magic[4] = { 0x57, 0x41, 0x4C, 0x00 };
		if (std::memcmp(header.magic, expected_magic, 4) != 0) {
			std::filesystem::remove(path_);
			return;
		}

		// sentinel file stores the last applied seq_no to prevent double-applying entries on repeated replays
		std::filesystem::path sentinel = path_;
		sentinel.replace_extension(".applied");

		uint64_t last_applied = 0;
		if (std::filesystem::exists(sentinel)) {
			std::ifstream sf(sentinel, std::ios::binary);
			sf.read(reinterpret_cast<char*>(&last_applied), sizeof(last_applied));
		}

		while (true) {
			WalEntryHeader entry{};
			in.read(reinterpret_cast<char*>(&entry), sizeof(entry));
			if (in.gcount() < static_cast<std::streamsize>(sizeof(entry))) break;

			std::vector<uint8_t> chunk(entry.chunk_data_size);
			in.read(reinterpret_cast<char*>(chunk.data()), entry.chunk_data_size);
			if (in.gcount() < static_cast<std::streamsize>(entry.chunk_data_size)) break;

			uint32_t stored_checksum = 0;
			in.read(reinterpret_cast<char*>(&stored_checksum), sizeof(stored_checksum));
			if (in.gcount() < static_cast<std::streamsize>(sizeof(stored_checksum))) break;

			if (compute_checksum(entry, chunk.data(), entry.chunk_data_size) != stored_checksum) break;

			if (entry.seq_no <= last_applied) continue;

			std::filesystem::path target(entry.target_file_path);
			{
				std::ofstream pulse(target, std::ios::binary | std::ios::app);
				pulse.write(reinterpret_cast<const char*>(chunk.data()), entry.chunk_data_size);
			}

			{
				std::ofstream sf(sentinel, std::ios::binary | std::ios::trunc);
				sf.write(reinterpret_cast<const char*>(&entry.seq_no), sizeof(entry.seq_no));
			}
		}
	}

	// xxhash32 over the entry header + chunk data to detect torn writes
	uint32_t WalManager::compute_checksum(const WalEntryHeader& header, const uint8_t* chunk_data, uint32_t chunk_data_size) {
		XXH32_state_t state;
		XXH32_reset(&state, 0);
		XXH32_update(&state, &header, sizeof(header));
		XXH32_update(&state, chunk_data, chunk_data_size);
		return XXH32_digest(&state);
	}

}

