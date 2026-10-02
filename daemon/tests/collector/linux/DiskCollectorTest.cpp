#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <unistd.h>

#include "DiskCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {

    class DiskCollectorTest : public ::testing::Test {
    protected:
        void SetUp() override { write_fixture(reading1); }

        void TearDown() override { std::filesystem::remove(fixture_path); }

        std::string fixture_path = "test_disk_stat";

        void write_fixture(const std::string& content) {
            std::ofstream out(fixture_path, std::ios::trunc);
            out << content;
        }
        // baseline fixture
        const std::string reading1 = "   8       0 sda 100 0 2000 50 200 0 4000 80 0 1000 1000 10 0 500 5\n"
                                     "   8       1 sda1 50 0 1000 20 100 0 2000 30 0 500 500 5 0 250 2\n"
                                     " 259       0 nvme0n1 300 0 6000 100 400 0 8000 150 0 2000 2000 20 0 1000 10\n"
                                     " 259       1 nvme0n1p1 10 0 200 5 20 0 400 8 0 100 100 2 0 50 1\n"
                                     "   7       0 loop0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
                                     " 252       0 dm-0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
                                     " 253       0 zram0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";

        // sda and nvme0n1 updated
        const std::string reading2 = "   8       0 sda 150 0 2200 60 250 0 4400 90 2 1100 1100 15 0 550 6\n"
                                     "   8       1 sda1 50 0 1000 20 100 0 2000 30 0 500 500 5 0 250 2\n"
                                     " 259       0 nvme0n1 350 0 6500 110 450 0 8500 160 1 2200 2200 25 0 1100 12\n"
                                     " 259       1 nvme0n1p1 10 0 200 5 20 0 400 8 0 100 100 2 0 50 1\n"
                                     "   7       0 loop0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
                                     " 252       0 dm-0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
                                     " 253       0 zram0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";

        // sdb appears for the firs time and sda and nvme0n1 advance
        const std::string reading3 = "   8       0 sda 200 0 2400 70 300 0 4800 100 1 1200 1200 20 0 600 7\n"
                                     "   8      16 sdb 40 0 800 10 50 0 1000 15 0 200 200 3 0 100 1\n"
                                     " 259       0 nvme0n1 400 0 7000 120 500 0 9000 170 0 2400 2400 30 0 1200 14\n";

        // too few fields invalid
        const std::string reading_invalid_short = "   8       0 sda 100 0 2000\n";

        // non number where number is expected
        const std::string reading_invalid_nonnumeric = "   8       0 sda abc 0 2000 50 200 0 4000 80 0 1000 1000 10 0 500 5\n";
    };

    // smoke test, mainly ensuring proc/diskstats is valid
    TEST_F(DiskCollectorTest, InitializeSucceedsWithValidFile) {
        DiskCollector collector(fixture_path);
        EXPECT_TRUE(collector.initialize());
    }

    // main check to see if diskcollector calculates correctly
    TEST_F(DiskCollectorTest, CollectComputesCorrectDeltas) {
        DiskCollector collector(fixture_path);
        ASSERT_TRUE(collector.initialize());

        // need to manually count and wait times here
        auto t0 = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::seconds(1));

        write_fixture(reading2);
        ASSERT_TRUE(collector.collect());
        auto t1 = std::chrono::steady_clock::now();

        double elapsed_sec = std::chrono::duration<double>(t1 - t0).count();

        MetricSnapshot snap;
        collector.fill_snapshot(snap);

        ASSERT_EQ(snap.disks.size(), 2);

        // grabbing sda and nvme drives to check values with
        const MetricSnapshot::DiskStats* sda = nullptr;
        const MetricSnapshot::DiskStats* nvme = nullptr;

        for (const auto& d : snap.disks) {
            if (d.device_name == "sda") {
                sda = &d;
            }
            if (d.device_name == "nvme0n1") {
                nvme = &d;
            }
        }

        // makes sure theyre there
        ASSERT_NE(sda, nullptr);
        ASSERT_NE(nvme, nullptr);

        EXPECT_NEAR(sda->read_bytes_per_sec, 102400.0 / elapsed_sec, 500.0);
        EXPECT_NEAR(sda->write_bytes_per_sec, 204800.0 / elapsed_sec, 500.0);
        EXPECT_EQ(sda->queue_depth, 2);
        EXPECT_NEAR(sda->discards_per_sec, 5.0 / elapsed_sec, 1.0);
        EXPECT_NEAR(sda->discard_bytes_per_sec, 25600.0 / elapsed_sec, 500.0);

        EXPECT_NEAR(nvme->read_bytes_per_sec, 256000.0 / elapsed_sec, 500.0);
        EXPECT_NEAR(nvme->write_bytes_per_sec, 256000.0 / elapsed_sec, 500.0);
        EXPECT_EQ(nvme->queue_depth, 1);
        EXPECT_NEAR(nvme->discards_per_sec, 5.0 / elapsed_sec, 1.0);
        EXPECT_NEAR(nvme->discard_bytes_per_sec, 51200.0 / elapsed_sec, 500.0);
    }

    // checks if device will appear in snapshot when it has no previous tick to comapare against
    TEST_F(DiskCollectorTest, NewDeviceNotReportedUntilSecondReading) {
        DiskCollector collector(fixture_path);
        ASSERT_TRUE(collector.initialize());

        write_fixture(reading2);
        ASSERT_TRUE(collector.collect());

        // adding in sdb for the first time
        write_fixture(reading3);
        ASSERT_TRUE(collector.collect());

        MetricSnapshot snap;
        collector.fill_snapshot(snap);

        for (const auto& d : snap.disks) {
            EXPECT_NE(d.device_name, "sdb"); // this is the first tick sdb is seen so must not appear (no baseline)
        }
    }

    // if proc/diskstats becomes malformed (here with a too short line) after the first read for some reason
    TEST_F(DiskCollectorTest, MalformedLineTooShortFails) {
        DiskCollector collector(fixture_path);
        ASSERT_TRUE(collector.initialize());

        write_fixture(reading_invalid_short);
        EXPECT_FALSE(collector.collect());
    }

    // if proc/diskstats becomes malformed (here non numeric at a numeric value spot) after the first read for some reason
    TEST_F(DiskCollectorTest, MalformedLineNonNumericFails) {
        DiskCollector collector(fixture_path);
        ASSERT_TRUE(collector.initialize());

        write_fixture(reading_invalid_nonnumeric);
        EXPECT_FALSE(collector.collect());
    }

} // namespace pulsedb
