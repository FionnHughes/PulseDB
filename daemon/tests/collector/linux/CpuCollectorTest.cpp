#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "CpuCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {

    class CpuCollectorTest : public ::testing::Test {
    protected:
        void SetUp() override { write_fixture(reading1); }

        void TearDown() override { std::filesystem::remove(fixture_path); }

        std::string fixture_path = "test_cpu_stat";

        void write_fixture(const std::string& content) {
            std::ofstream out(fixture_path, std::ios::trunc);
            out << content;
        }

        std::string reading1 = "cpu  10000 200 3000 50000 5000 100 50 10 0 0\n"
                               "cpu0 5000 0 0 20000 1000 0 0 0 0 0\n"
                               "cpu1 6000 0 0 25000 2000 0 0 0 0 0\n"
                               "intr 12345 0 0 0\n"
                               "ctxt 98765\n"
                               "btime 1700000000";
        std::string reading2 = "cpu  10500 200 3000 50400 5100 100 50 10 0 0\n"
                               "cpu0 5250 0 0 20650 1100 0 0 0 0 0\n"
                               "cpu1 6700 0 0 25200 2100 0 0 0 0 0\n"
                               "intr 12400 0 0 0\n"
                               "ctxt 98800\n"
                               "btime 1700000000\n";
        std::string reading_invalid = "cpu  10500 200\n"
                                      "cpu0 5250 0\n"
                                      "cpu1 6700 0\n"
                                      "intr 12400 0 0 0\n"
                                      "ctxt 98800\n"
                                      "btime 1700000000\n";
    };

    // smoke test, mainly ensuring proc/stat is valid
    TEST_F(CpuCollectorTest, InitializeSucceedsWithValidFile) {
        CpuCollector collector(fixture_path);
        EXPECT_TRUE(collector.initialize());
    }

    TEST_F(CpuCollectorTest, InitializeFailsWithMissingFile) {
        CpuCollector collector("invalid_file");
        EXPECT_FALSE(collector.initialize());
    }

    // testing with an invalid proc/stat (too few columns)
    TEST_F(CpuCollectorTest, InitializeFailsWithMalformedFile) {
        write_fixture(reading_invalid);
        CpuCollector collector(fixture_path);
        EXPECT_FALSE(collector.initialize());
    }

    // checks proper usage
    TEST_F(CpuCollectorTest, CollectComputesCorrectPercentage) {
        CpuCollector collector(fixture_path);
        MetricSnapshot snap;

        collector.initialize();
        write_fixture(reading2);

        ASSERT_TRUE(collector.collect());
        collector.fill_snapshot(snap);

        EXPECT_NEAR(snap.cpu_total_percent, 50.0f, 0.01f);
        EXPECT_NEAR(snap.cpu_iowait_percent, 10.0f, 0.01f);
        EXPECT_NEAR(snap.cpu_steal_percent, 0.0f, 0.01f);
        EXPECT_NEAR(snap.cpu_per_core_percent[0], 25.0f, 0.01f);
        EXPECT_NEAR(snap.cpu_per_core_percent[1], 70.0f, 0.01f);
    }

    // if proc/stat becomes malformed after the first read for some reason this will catch it
    TEST_F(CpuCollectorTest, CollectReturnsFalseOnUnreadableSecondPass) {
        CpuCollector collector(fixture_path);
        MetricSnapshot snap;

        collector.initialize();
        write_fixture(reading_invalid);

        ASSERT_FALSE(collector.collect());
    }

} // namespace pulsedb
