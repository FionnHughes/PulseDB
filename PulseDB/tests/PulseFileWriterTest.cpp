#include <gtest/gtest.h>
#include <fstream>
#include "storage/PulseFileWriter.h"

class PulseFileWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // runs before every test
        pulsedb::PulseFileWriter writer(test_filepath, MetricType::cpu_total, "cpu_total");
        writer.open();

        for (int i = 0; i < 60; i++) {
            pulsedb::MetricReading reading;
            reading.timestamp_ms = base_ts + (i * 1000);
            reading.value = 10.0 + i;
            writer.append(reading);
        }

        writer.flush();
        writer.close();
    }

    void TearDown() override {
        // runs after every test
        std::remove(test_filepath.c_str());
    }

    // shared members all tests can access
    std::string test_filepath = "test_output.pulse";
    int64_t base_ts = 1700000000000;
};

TEST_F(PulseFileWriterTest, HeaderIsCorrect) {
    std::ifstream file(test_filepath, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    FileHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    ASSERT_TRUE(file.good());

    EXPECT_EQ(std::string(header.magic, 4), "PULS");
    EXPECT_EQ(header.version, 1);
    EXPECT_EQ(header.metric_type_id, static_cast<uint8_t>(MetricType::cpu_total));
    EXPECT_EQ(header.reserved, 0x00);
    EXPECT_EQ(header.chunk_count, 1);
    EXPECT_EQ(header.chunk_index_offset, 64);
    EXPECT_EQ(header.readings_per_chunk, 60);
    EXPECT_EQ(header.collection_interval_ms, 1000);
    EXPECT_EQ(std::string(header.metric_name, 9), "cpu_total");
}

TEST_F(PulseFileWriterTest, IndexEntryIsCorrect) {
    std::ifstream file(test_filepath, std::ios::binary);
    file.seekg(64, std::ios::beg);

    ChunkIndexEntry chunk_index_entry{};
    file.read(reinterpret_cast<char*>(&chunk_index_entry), sizeof(chunk_index_entry));

    EXPECT_EQ(chunk_index_entry.chunk_start_ts, 1700000000000);
    EXPECT_EQ(chunk_index_entry.byte_offset, 23104);
    EXPECT_EQ(chunk_index_entry.compressed_size, 616);
}

TEST_F(PulseFileWriterTest, ChunkDataIsCorrect) {
    std::ifstream file(test_filepath, std::ios::binary);
    file.seekg(23104, std::ios::beg);

    ChunkHeader chunk_header{};
    file.read(reinterpret_cast<char*>(&chunk_header), sizeof(chunk_header));

    EXPECT_EQ(chunk_header.base_timestamp_ms, 1700000000000);
    EXPECT_EQ(chunk_header.reading_count, 60);
    EXPECT_EQ(chunk_header.reserved, 0x0000);
    EXPECT_EQ(chunk_header.uncompressed_size, 616);

    uint16_t first_delta = 0;
    file.read(reinterpret_cast<char*>(&first_delta), sizeof(first_delta));
    EXPECT_EQ(first_delta, 0);

    double first_value = 0.0;
    file.read(reinterpret_cast<char*>(&first_value), sizeof(first_value));
    EXPECT_DOUBLE_EQ(first_value, 10.0);
}
// cd C:\Users\fionn\git\.full_projects\VScpp\PulseDB\build\PulseDB\Release
// .\pulsedb_tests.exe