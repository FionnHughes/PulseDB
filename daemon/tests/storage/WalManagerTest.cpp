#include <gtest/gtest.h>

#include "storage/WalManager.h"

namespace pulsedb {

    class WalManagerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            temp_dir = std::filesystem::temp_directory_path() / "waltest";
            std::filesystem::create_directories(temp_dir);
            wal_path = temp_dir / "test.wal";
            target_path = temp_dir / "test.pulse";
        }

        void TearDown() override {
            std::filesystem::remove_all(temp_dir);
        }

        std::filesystem::path temp_dir;
        std::filesystem::path wal_path;
        std::filesystem::path target_path;
    };

    // basic use with, append a chunk, replay the WAL and then check the data ended up in the target file
    TEST_F(WalManagerTest, AppendAndReplay) {
        std::vector<uint8_t> chunk_data(100, 0xAB);
        {
            WalManager manager(wal_path);            
            manager.append(target_path, chunk_data);
        }
    
        WalManager manager2(wal_path);
        manager2.replay();

        std::ifstream result(target_path, std::ios::binary);
        std::vector<char> result_bytes{
            std::istreambuf_iterator<char>(result),
            std::istreambuf_iterator<char>{}
        };
        std::vector<char> expected(chunk_data.begin(), chunk_data.end());

        ASSERT_EQ(result_bytes.size(), expected.size());
        ASSERT_TRUE(std::equal(result_bytes.begin(), result_bytes.end(), expected.begin()));
    }

    // cuts the WAL to simulate a bad write mid entry and the replay should skip the corrupt entry and shoudnt touch the target file
    TEST_F(WalManagerTest, TornWriteDetection) {
        std::vector<uint8_t> chunk_data(100, 0xAB);
        {
            WalManager manager(wal_path);            
            manager.append(target_path, chunk_data);
        }

        std::filesystem::resize_file(wal_path, std::filesystem::file_size(wal_path) - 10);
    
        WalManager manager2(wal_path);
        manager2.replay();

        ASSERT_FALSE(std::filesystem::exists(target_path));
    }

    // if the sentinel says an entry was already applied, replay should not write it again
    TEST_F(WalManagerTest, AlreadyApplied) {
        std::vector<uint8_t> chunk_data(100, 0xAB);
        {
            WalManager manager(wal_path);            
            manager.append(target_path, chunk_data);
        }
        {
            std::ofstream out(target_path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(chunk_data.data()), chunk_data.size());
        }
        {
            std::filesystem::path sentinel = wal_path;
            sentinel.replace_extension(".applied");
            uint64_t applied_seq = 1;
            std::ofstream sf(sentinel, std::ios::binary | std::ios::trunc);
            sf.write(reinterpret_cast<const char*>(&applied_seq), sizeof(applied_seq));
        }

        WalManager manager2(wal_path);
        manager2.replay();

        ASSERT_EQ(std::filesystem::file_size(target_path), static_cast<std::uintmax_t>(chunk_data.size()));
    }

    // simulates the real case where the .pulse file already has a header before the crash where replay appends the chunk after the existing content
    TEST_F(WalManagerTest, AppendAndReplayWithExistingFile) {
        //make a .pulse file with a 23104-byte header and index already written
        //exactly what PulseFileWriter creates on open() for a new file
        constexpr size_t PULSE_HEADER_SIZE = 23104;
        {
            std::ofstream preexist(target_path, std::ios::binary);
            std::vector<uint8_t> header_bytes(PULSE_HEADER_SIZE, 0x00);
            preexist.write(reinterpret_cast<const char*>(header_bytes.data()), header_bytes.size());
        }

        std::vector<uint8_t> chunk_data(100, 0xAB);
        {
            WalManager manager(wal_path);
            manager.append(target_path, chunk_data);
            //if crash here, chunk was not written to .pulse
        }

        // First replay: should append the chunk
        {
            WalManager manager2(wal_path);
            manager2.replay();
        }
        ASSERT_EQ(std::filesystem::file_size(target_path), static_cast<std::uintmax_t>(PULSE_HEADER_SIZE + chunk_data.size()));

        // Second replay: sentinel prevents double-write
        {
            WalManager manager3(wal_path);
            manager3.replay();
        }
        ASSERT_EQ(std::filesystem::file_size(target_path), static_cast<std::uintmax_t>(PULSE_HEADER_SIZE + chunk_data.size()));
    }
}

