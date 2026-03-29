#include <gtest/gtest.h>
#include <fstream>
#include "storage/PulseFileWriter.h"
#include "storage/PulseFileReader.h"

class PulseFileReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
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

        reader.open();
    }

    void TearDown() override {
        reader.close();
        std::remove(test_filepath.c_str());
    }

    std::string test_filepath = "test_output.pulse";
    int64_t base_ts = 1700000000000;
    pulsedb::PulseFileReader reader{ test_filepath };
};


TEST_F(PulseFileReaderTest, FullQuery) {
    auto results = reader.query(base_ts, base_ts + 59000);

    EXPECT_EQ(results.size(), 60);
    EXPECT_EQ(results.front().timestamp_ms, base_ts);
    EXPECT_EQ(results.back().timestamp_ms, base_ts + 59000);
}

TEST_F(PulseFileReaderTest, PartialQuery) {
    auto results = reader.query(base_ts + 10000, base_ts + 19000);

    EXPECT_EQ(results.size(), 10);
    EXPECT_EQ(results.front().timestamp_ms, base_ts + 10000);
    EXPECT_EQ(results.back().timestamp_ms, base_ts + 19000);
}

TEST_F(PulseFileReaderTest, EmptyQuery) {
    auto results = reader.query(base_ts + 100000, base_ts + 200000);

    EXPECT_EQ(results.size(), 0);
}
/*

cd C:\Users\fionn\git\.full_projects\VScpp\PulseDB\build\PulseDB\Release
.\pulsedb_tests.exe

*/
