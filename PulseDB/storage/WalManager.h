#pragma once
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <vector>
#include <functional>

namespace pulsedb {

#pragma pack(push, 1)
    struct WalFileHeader {
        uint8_t magic[4];  // 0x57 0x41 0x4C 0x00
    };

    struct WalEntryHeader {
        uint64_t seq_no;
        char     target_file_path[256];  // null-padded UTF-8
        uint32_t chunk_data_size;
        // followed by: chunk_data (chunk_data_size bytes)
        // followed by: checksum (uint32_t) — xxHash32 of full entry
    };
#pragma pack(pop)

    static_assert(sizeof(WalFileHeader) == 4);
    static_assert(sizeof(WalEntryHeader) == 268);

    class WalManager {
    public:
        explicit WalManager(const std::filesystem::path& wal_path);
        ~WalManager() = default;

        void append(const std::filesystem::path& target_file, const std::vector<uint8_t>& chunk_data);

        void replay();

        uint64_t current_seq() const { return seq_; }

    private:
        std::filesystem::path path_;
        std::ofstream out_;
        uint64_t seq_ = 0;

        static uint32_t compute_checksum(const WalEntryHeader& header, const uint8_t* chunk_data, uint32_t chunk_data_size);
        void write_file_header(std::ofstream& stream);
    };

} // namespace pulsedb