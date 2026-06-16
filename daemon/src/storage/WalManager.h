#pragma once
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <vector>
#include <functional>

namespace pulsedb {

    #pragma pack(push, 1)
    // just 4 magic bytes at the start of every .wal file - used to check the file isn't truncated or corrupt
    struct WalFileHeader {
        uint8_t magic[4];  // 0x57 0x41 0x4C 0x00
    };

    // header for each WAL entry containing sequence number, the target .pulse file path, and how many bytes of chunk data follow it
    struct WalEntryHeader {
        uint64_t seq_no;
        char     target_file_path[256];
        uint32_t chunk_data_size;
    };
    #pragma pack(pop)

    // making sure the packed structs are exactly the right size to layout problems at compile time
    static_assert(sizeof(WalFileHeader) == 4);
    static_assert(sizeof(WalEntryHeader) == 268);

    // write-ahead log which logs every compressed chunk to a .wal file before writing to .pulse so we can recover from crashes
    class WalManager {
    public:
        explicit WalManager(const std::filesystem::path& wal_path);
        ~WalManager() = default;

        bool append(const std::filesystem::path& target_file, const std::vector<uint8_t>& chunk_data);
        void clear();
        void replay();

        uint64_t current_seq() const { return seq_; }

    private:
        std::filesystem::path path_;
        std::ofstream out_;

        // sequence number, goes up with every appended entry, the sentinel file tracks what's already been replayed
        uint64_t seq_ = 0;

        static uint32_t compute_checksum(const WalEntryHeader& header, const uint8_t* chunk_data, uint32_t chunk_data_size);
        void write_file_header(std::ofstream& stream);
    };

}