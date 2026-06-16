#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

#include "queue/SpscQueue.h"
#include "storage/StorageEngine.h"
#include "storage/PulseFileWriter.h"
#include "storage/PulseFileReader.h"

namespace pulsedb {

    class StorageEngineTest : public ::testing::Test {
    protected:
        // creates two .pulse files on different days before each test
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

    // queries a time range within a single day
    TEST_F(StorageEngineTest, SingleFile) {
        auto results = engine.query("cpu_total", base_ts, base_ts + 59000);

        EXPECT_EQ(results.size(), 60);
        EXPECT_EQ(results.front().timestamp_ms, base_ts);
        EXPECT_EQ(results.back().timestamp_ms, base_ts + 59000);
    }

    // queries across two days which should get results from both files
    TEST_F(StorageEngineTest, MultiFile) {
        auto results = engine.query("cpu_total", base_ts, (base_ts + 86400000) + 59000);

        EXPECT_EQ(results.size(), 120);
        EXPECT_EQ(results.front().timestamp_ms, base_ts);
        EXPECT_EQ(results.back().timestamp_ms, (base_ts + 86400000) + 59000);
    }

    // queries a day with no file which should return empty
    TEST_F(StorageEngineTest, MissingFile) {
        auto results = engine.query("cpu_total", base_ts + (86400000 * 2), base_ts + (86400000 * 2) + 59000);

        EXPECT_EQ(results.size(), 0);
    }

    // pushes 60 snapshots through the full write pipeline and checks they all show up
    TEST_F(StorageEngineTest, WriterPipelineRoundTrip) {
        SpscQueue<MetricSnapshot, 1024> queue;
        StorageEngine engine2{ test_dir + "_pipeline" };
        std::filesystem::create_directories(test_dir + "_pipeline");
        engine2.open();
        engine2.start_writer(queue);

        int64_t now = 1700000000000LL;
        for (int i = 0; i < 60; i++) {
            MetricSnapshot snap{};
            snap.timestamp_ms = now + (i * 1000LL);
            snap.cpu_total_percent = static_cast<float>(i + 1);
            while (!queue.try_push(snap)) {}
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        engine2.close();

        auto results = engine2.query("cpu_total", now, now + 59000LL);
        EXPECT_EQ(results.size(), 60u);

        std::filesystem::remove_all(test_dir + "_pipeline");
    }

    // pushes a snapshot just before midnight and one just after to check that two separate .pulse files were created
    TEST_F(StorageEngineTest, MidnightRolloverCreatesNewFile) {
        SpscQueue<MetricSnapshot, 1024> queue;
        std::string rollover_dir = test_dir + "_rollover";
        std::filesystem::create_directories(rollover_dir);
        StorageEngine engine3{ rollover_dir };
        engine3.open();
        engine3.start_writer(queue);

        //2023-11-14 23:59:59.000 UTC  and  2023-11-15 00:00:01.000 UTC
        int64_t ts_before = 1700006399000LL;
        int64_t ts_after = 1700006401000LL;

        MetricSnapshot s1{};
        s1.timestamp_ms = ts_before;
        s1.cpu_total_percent = 10.0f;
        queue.try_push(s1);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        MetricSnapshot s2{};
        s2.timestamp_ms = ts_after;
        s2.cpu_total_percent = 20.0f;
        queue.try_push(s2);

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        engine3.close();

        EXPECT_TRUE(std::filesystem::exists(rollover_dir + "/cpu_total/2023-11-14.pulse"));
        EXPECT_TRUE(std::filesystem::exists(rollover_dir + "/cpu_total/2023-11-15.pulse"));

        std::filesystem::remove_all(rollover_dir);
    }
}