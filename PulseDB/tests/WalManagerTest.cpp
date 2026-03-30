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

        TEST_F(WalManagerTest, AlreadyApplied) {
            std::vector<uint8_t> chunk_data(100, 0xAB);
            {
                WalManager manager(wal_path);            
                manager.append(target_path, chunk_data);
            }
            std::ofstream out(target_path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(chunk_data.data()), chunk_data.size());

            WalManager manager2(wal_path);
            manager2.replay();

            ASSERT_EQ(std::filesystem::file_size(target_path), static_cast<std::uintmax_t>(chunk_data.size()));
        }
    }

