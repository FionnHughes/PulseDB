#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <unistd.h>

#include "NetworkCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {

    class NetworkCollectorTest : public ::testing::Test {
    protected:
        void SetUp() override { write_fixture(reading1); }

        void TearDown() override { std::filesystem::remove(fixture_path); }

        std::string fixture_path = "test_net_dev";

        void write_fixture(const std::string& content) {
            std::ofstream out(fixture_path, std::ios::trunc);
            out << content;
        }
        // baseline fixture
        const std::string reading1 =
            "Inter-|   Receive                                                |  Transmit\n"
            " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
            "    lo: 5000      40    0    0    0     0          0         0     5000      40    0    0    0     0       0          0\n"
            "  eth0: 100000   500    2    5    0     0          0         0    80000     400    1    3    0     0       0          0\n"
            "  wlan0: 200000   800    0    1    0     0          0         0   150000     700    0    2    0     0       0          0\n";

        // eth0 and wlan0 advanced
        const std::string reading2 =
            "Inter-|   Receive                                                |  Transmit\n"
            " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
            "    lo: 5000      40    0    0    0     0          0         0     5000      40    0    0    0     0       0          0\n"
            "  eth0: 150000   600    3    7    0     0          0         0   100000     450    2    4    0     0       0          0\n"
            "  wlan0: 260000   950    0    2    0     0          0         0   180000     820    0    3    0     0       0          0\n";

        // docker0 appears mid run, tests will it try compute with no previous values to diff
        const std::string reading3 =
            "Inter-|   Receive                                                |  Transmit\n"
            " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
            "    lo: 5000      40    0    0    0     0          0         0     5000      40    0    0    0     0       0          0\n"
            "  eth0: 200000   700    3    8    0     0          0         0   120000     500    2    5    0     0       0          0\n"
            "  wlan0: 320000  1100   0    3    0     0          0         0   210000     900    0    4    0     0       0          0\n"
            "docker0: 1000     10    0    0    0     0          0         0     1000      10    0    0    0     0       0          0\n";

        // too few lines
        const std::string reading_invalid_short = "  eth0: 100000   500\n";

        // non numeric where we expect numeric
        const std::string reading_invalid_nonnumeric =
            "  eth0: abc   500    2    5    0     0          0         0    80000     400    1    3    0     0       0          0\n";
    };

    // smoke test, mainly ensuring proc/net/dev is valid
    TEST_F(NetworkCollectorTest, InitializeSucceedsWithValidFile) {
        NetworkCollector collector(fixture_path);
        EXPECT_TRUE(collector.initialize());
    }

    // main check to see if diskcollector calculates correctly
    TEST_F(NetworkCollectorTest, CollectComputesCorrectDeltas) {
        NetworkCollector collector(fixture_path);
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

        ASSERT_EQ(snap.network_adapters.size(), 2);

        // grabbing sda and nvme drives to check values with
        const MetricSnapshot::NetworkStats* eth0 = nullptr;
        const MetricSnapshot::NetworkStats* wlan0 = nullptr;

        for (const auto& d : snap.network_adapters) {
            if (d.adapter_name == "eth0") {
                eth0 = &d;
            }
            if (d.adapter_name == "wlan0") {
                wlan0 = &d;
            }
        }

        // makes sure theyre there before computation
        ASSERT_NE(eth0, nullptr);
        ASSERT_NE(wlan0, nullptr);

        EXPECT_NEAR(eth0->bytes_in_per_sec, 50000.0 / elapsed_sec, 500.0);
        EXPECT_NEAR(eth0->bytes_out_per_sec, 20000.0 / elapsed_sec, 500.0);
        EXPECT_NEAR(eth0->packets_in_per_sec, 100.0 / elapsed_sec, 5.0);
        EXPECT_NEAR(eth0->packets_out_per_sec, 50.0 / elapsed_sec, 5.0);
        EXPECT_NEAR(eth0->rx_errors_per_sec, 1.0 / elapsed_sec, 1.0);
        EXPECT_NEAR(eth0->tx_errors_per_sec, 1.0 / elapsed_sec, 1.0);
        EXPECT_NEAR(eth0->rx_dropped_per_sec, 2.0 / elapsed_sec, 1.0);
        EXPECT_NEAR(eth0->tx_dropped_per_sec, 1.0 / elapsed_sec, 1.0);

        EXPECT_NEAR(wlan0->bytes_in_per_sec, 60000.0 / elapsed_sec, 500.0);
        EXPECT_NEAR(wlan0->bytes_out_per_sec, 30000.0 / elapsed_sec, 500.0);
        EXPECT_NEAR(wlan0->packets_in_per_sec, 150.0 / elapsed_sec, 5.0);
        EXPECT_NEAR(wlan0->packets_out_per_sec, 120.0 / elapsed_sec, 5.0);
        EXPECT_NEAR(wlan0->rx_errors_per_sec, 0.0 / elapsed_sec, 1.0);
        EXPECT_NEAR(wlan0->tx_errors_per_sec, 0.0 / elapsed_sec, 1.0);
        EXPECT_NEAR(wlan0->rx_dropped_per_sec, 1.0 / elapsed_sec, 1.0);
        EXPECT_NEAR(wlan0->tx_dropped_per_sec, 1.0 / elapsed_sec, 1.0);
    }

    // checks if device will appear in snapshot when it has no previous tick to comapare against
    TEST_F(NetworkCollectorTest, NewDeviceNotReportedUntilSecondReading) {
        NetworkCollector collector(fixture_path);
        ASSERT_TRUE(collector.initialize());

        write_fixture(reading2);
        ASSERT_TRUE(collector.collect());

        // adding in docker0 for the first time
        write_fixture(reading3);
        ASSERT_TRUE(collector.collect());

        MetricSnapshot snap;
        collector.fill_snapshot(snap);

        for (const auto& n : snap.network_adapters) {
            EXPECT_NE(n.adapter_name, "docker0"); // this is the first tick sdb is seen so must not appear (no baseline)
        }
    }

    // if proc/diskstats becomes malformed (here with a too short line) after the first read for some reason
    TEST_F(NetworkCollectorTest, MalformedLineTooShortFails) {
        NetworkCollector collector(fixture_path);
        ASSERT_TRUE(collector.initialize());

        write_fixture(reading_invalid_short);
        EXPECT_FALSE(collector.collect());
    }

    // if proc/diskstats becomes malformed (here non numeric at a numeric value spot) after the first read for some reason
    TEST_F(NetworkCollectorTest, MalformedLineNonNumericFails) {
        NetworkCollector collector(fixture_path);
        ASSERT_TRUE(collector.initialize());

        write_fixture(reading_invalid_nonnumeric);
        EXPECT_FALSE(collector.collect());
    }

} // namespace pulsedb
