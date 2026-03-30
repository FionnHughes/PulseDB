#include <xxhash.h>

#include "WalManager.h"

namespace pulsedb{

	WalManager::WalManager(const std::filesystem::path& wal_path) : path_(wal_path) {

		if (!std::filesystem::exists(path_)) {
			std::ofstream init(path_, std::ios::binary);
			write_file_header(init);
			init.close();
		}

		out_.open(path_, std::ios::binary | std::ios::app);
		if (!out_.is_open())
			throw std::runtime_error("WAL: failed to open file for writing: " + path_.string());
		std::ifstream in(path_, std::ios::binary);

		WalFileHeader header{};
		in.read(reinterpret_cast<char*>(&header), sizeof(header));

		constexpr uint8_t expected_magic[4] = { 0x57, 0x41, 0x4C, 0x00 };
		if (std::memcmp(header.magic, expected_magic, 4) != 0)
			return;
		while (true) {
			WalEntryHeader entry{};
			in.read(reinterpret_cast<char*>(&entry), sizeof(entry));
			if (in.gcount() < sizeof(entry)) break;

			std::vector<uint8_t> chunk(entry.chunk_data_size);
			in.read(reinterpret_cast<char*>(chunk.data()), entry.chunk_data_size);
			if (in.gcount() < entry.chunk_data_size) break;
			
			uint32_t stored_checksum = 0;
			in.read(reinterpret_cast<char*>(&stored_checksum), sizeof(stored_checksum));
			if (in.gcount() < sizeof(stored_checksum)) break;

			if (compute_checksum(entry, chunk.data(), entry.chunk_data_size) != stored_checksum) break;
	
			seq_ = entry.seq_no;
		}
	}

	void WalManager::write_file_header(std::ofstream& file) {
		file.seekp(0, std::ios::beg);
		WalFileHeader header{ {0x57, 0x41, 0x4C, 0x00} };
		file.write(reinterpret_cast<char*>(&header), sizeof(header));
		file.flush();
	}

	void WalManager::append(const std::filesystem::path& target_file, const std::vector<uint8_t>& chunk_data) {
		seq_++;
		WalEntryHeader entry{};
		entry.seq_no = seq_;
		memcpy(entry.target_file_path, target_file.string().c_str(), target_file.string().size());
		entry.chunk_data_size = sizeof(chunk_data);
		
		out_.write(reinterpret_cast<char*>(&entry), sizeof(entry));
		out_.write(reinterpret_cast<const char*>(chunk_data.data()), entry.chunk_data_size);

		uint32_t checksum = compute_checksum(entry, chunk_data.data(), entry.chunk_data_size);
		out_.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));

		out_.flush();
	}

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

		std::unordered_map<std::string, uint64_t> bytes_seen;

		while (true) {
			WalEntryHeader entry{};
			in.read(reinterpret_cast<char*>(&entry), sizeof(entry));
			if (in.gcount() < sizeof(entry)) break;

			std::vector<uint8_t> chunk(entry.chunk_data_size);
			in.read(reinterpret_cast<char*>(chunk.data()), entry.chunk_data_size);
			if (in.gcount() < entry.chunk_data_size) break;

			uint32_t stored_checksum = 0;
			in.read(reinterpret_cast<char*>(&stored_checksum), sizeof(stored_checksum));
			if (in.gcount() < sizeof(stored_checksum)) break;

			if (compute_checksum(entry, chunk.data(), entry.chunk_data_size) != stored_checksum) break;

			std::filesystem::path target(entry.target_file_path);

			uint64_t current_size = std::filesystem::exists(target) ? std::filesystem::file_size(target) : 0;

			std::string key = target.string();
			uint64_t seen = bytes_seen[key];

			if (current_size >= seen + entry.chunk_data_size) {

			}
			else {
				std::ofstream pulse(target, std::ios::binary | std::ios::app);
				pulse.write(reinterpret_cast<const char*>(chunk.data()), entry.chunk_data_size);
			}
			bytes_seen[key] += entry.chunk_data_size;
			seq_ = entry.seq_no;
		}
	}

	uint32_t WalManager::compute_checksum(const WalEntryHeader& header, const uint8_t* chunk_data, uint32_t chunk_data_size) {
		XXH32_state_t* state = XXH32_createState();
		XXH32_reset(state, 0);                                        // seed = 0
		XXH32_update(state, &header, sizeof(header));                 // feed header bytes
		XXH32_update(state, chunk_data, chunk_data_size);             // feed chunk bytes
		uint32_t result = XXH32_digest(state);
		XXH32_freeState(state);
		return result;
	}

}

