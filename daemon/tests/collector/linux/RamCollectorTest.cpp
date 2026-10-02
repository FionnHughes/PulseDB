#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "RamCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {

    class RamCollectorTest : public ::testing::Test {
    protected:
        void SetUp() override { write_fixture(reading1); }

        void TearDown() override { std::filesystem::remove(fixture_path); }

        std::string fixture_path = "test_ram_stat";

        void write_fixture(const std::string& content) {
            std::ofstream out(fixture_path, std::ios::trunc);
            out << content;
        }

        std::string reading1 = "MemTotal:        1000000 kB\n"
                               "MemFree:          150000 kB\n"
                               "MemAvailable:     400000 kB\n"
                               "Buffers:           10000 kB\n"
                               "Cached:            20000 kB\n"
                               "SwapCached:            0 kB\n"
                               "Active:           300000 kB\n"
                               "Inactive:         200000 kB\n"
                               "SwapTotal:        200000 kB\n"
                               "SwapFree:          50000 kB\n"
                               "Dirty:               128 kB\n"
                               "Writeback:             0 kB\n"
                               "Slab:               5000 kB\n";

        // missing MemTotal in reading2
        std::string reading2 = "MemFree:          150000 kB\n"
                               "MemAvailable:     400000 kB\n"
                               "Buffers:           10000 kB\n"
                               "Cached:            20000 kB\n"
                               "SwapCached:            0 kB\n"
                               "Active:           300000 kB\n"
                               "Inactive:         200000 kB\n"
                               "SwapTotal:        200000 kB\n"
                               "SwapFree:          50000 kB\n"
                               "Dirty:               128 kB\n"
                               "Writeback:             0 kB\n"
                               "Slab:               5000 kB\n";
    };

    // smoke test, ensuring proc/meminfo is valid
    TEST_F(RamCollectorTest, InitializeSucceedsWithValidFile) {
        RamCollector collector(fixture_path);
        EXPECT_TRUE(collector.initialize());
    }

    TEST_F(RamCollectorTest, InitializeFailsWithMissingFile) {
        RamCollector collector("invalid_file");
        EXPECT_FALSE(collector.initialize());
    }

    // testing with an invalid proc/meminfo missing MemTotal
    TEST_F(RamCollectorTest, CollectFailsWithMalformedFile) {
        write_fixture(reading2);
        RamCollector collector(fixture_path);
        ASSERT_TRUE(collector.initialize());
        EXPECT_FALSE(collector.collect());
    }

    // checks proper usage
    TEST_F(RamCollectorTest, CollectComputesCorrectByteValues) {
        write_fixture(reading1);

        RamCollector collector(fixture_path);
        MetricSnapshot snap;

        ASSERT_TRUE(collector.initialize());
        ASSERT_TRUE(collector.collect());
        collector.fill_snapshot(snap);

        EXPECT_EQ(snap.ram_total_bytes, 1024000000ULL);
        EXPECT_EQ(snap.ram_available_bytes, 409600000ULL);
        EXPECT_EQ(snap.ram_used_bytes, 614400000ULL);
        EXPECT_EQ(snap.swap_total_bytes, 204800000ULL);
        EXPECT_EQ(snap.swap_used_bytes, 153600000ULL);
        EXPECT_EQ(snap.page_cache_bytes, 30720000ULL);
    }

    // if proc/meminfo is reordered
    TEST_F(RamCollectorTest, CollectHandlesUnorderedFields) {
        std::string reordered = "SwapFree:          50000 kB\n"
                                "MemAvailable:     400000 kB\n"
                                "Cached:            20000 kB\n"
                                "MemTotal:        1000000 kB\n"
                                "SwapTotal:        200000 kB\n"
                                "Buffers:           10000 kB\n";

        write_fixture(reordered);

        RamCollector collector(fixture_path);
        MetricSnapshot snap;

        ASSERT_TRUE(collector.initialize());
        ASSERT_TRUE(collector.collect());
        collector.fill_snapshot(snap);

        EXPECT_EQ(snap.ram_total_bytes, 1024000000ULL);
        EXPECT_EQ(snap.ram_available_bytes, 409600000ULL);
        EXPECT_EQ(snap.ram_used_bytes, 614400000ULL);
        EXPECT_EQ(snap.swap_total_bytes, 204800000ULL);
        EXPECT_EQ(snap.swap_used_bytes, 153600000ULL);
        EXPECT_EQ(snap.page_cache_bytes, 30720000ULL);
    }

} // namespace pulsedb
