#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

#include "storage/StorageEngine.h"
#include "storage/PulseFileWriter.h"
#include "storage/PulseFileReader.h"

namespace pulsedb {

    class StorageEngineTest : public ::testing::Test {
    protected:
        void SetUp() override {
            std::filesystem::create_directories(test_dir + "/cpu_total");


           PulseFileWriter writer(test_filepath, MetricType::cpu_total, "cpu_total", "test_data/test.wal");
            writer.open();

            for (int i = 0; i < 60; i++) {
               MetricReading reading;
                reading.timestamp_ms = base_ts + (i * 1000);
                reading.value = 10.0 + i;
                writer.append(reading);
            }
            writer.flush();
            writer.close();

            std::string test_filepath2 = "test_data/cpu_total/2023-11-15.pulse";
           PulseFileWriter writer2(test_filepath2, MetricType::cpu_total, "cpu_total", "test_data/test.wal");
            writer2.open();

            for (int i = 0; i < 60; i++) {
               MetricReading reading;
                reading.timestamp_ms = (base_ts + 86400000) + (i * 1000);
                reading.value = 20.0 + i;
                writer2.append(reading);
            }

            writer2.flush();
            writer2.close();
        }

        void TearDown() override {
            std::filesystem::remove_all(test_dir);
        }
        std::string test_dir = "test_data";

        std::string test_filepath = "test_data/cpu_total/2023-11-14.pulse";
        int64_t base_ts = 1700000000000;
       StorageEngine engine{ test_dir };
    };

    TEST_F(StorageEngineTest, SingleFile) {
        auto results = engine.query("cpu_total", base_ts, base_ts + 59000);

        EXPECT_EQ(results.size(), 60);
        EXPECT_EQ(results.front().timestamp_ms, base_ts);
        EXPECT_EQ(results.back().timestamp_ms, base_ts + 59000);
    }

    TEST_F(StorageEngineTest, MultiFile) {
        auto results = engine.query("cpu_total", base_ts, (base_ts + 86400000) + 59000);

        EXPECT_EQ(results.size(), 120);
        EXPECT_EQ(results.front().timestamp_ms, base_ts);
        EXPECT_EQ(results.back().timestamp_ms, (base_ts + 86400000) + 59000);
    }

    TEST_F(StorageEngineTest, MissingFile) {
        auto results = engine.query("cpu_total", base_ts + (86400000 * 2), base_ts + (86400000 * 2) + 59000);

        EXPECT_EQ(results.size(), 0);
    }
}

/*

cd C:\Users\fionn\git\.full_projects\VScpp\PulseDB\build\PulseDB\Release
.\pulsedb_tests.exe

*/