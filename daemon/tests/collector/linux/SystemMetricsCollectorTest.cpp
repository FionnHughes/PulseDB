#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>
#include <unistd.h>

#include "SystemMetricsCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {

    class SystemMetricsCollectorTest : public ::testing::Test {
    protected:
        void SetUp() override {
            write_fixture(stat_path, stat1);
            write_fixture(vmstat_path, vmstat1);
        }

        void TearDown() override {
            std::filesystem::remove(stat_path);
            std::filesystem::remove(vmstat_path);
        }

        std::string stat_path = "test_sysmetrics_stat";
        std::string vmstat_path = "test_sysmetrics_vmstat";

        void write_fixture(const std::string& path, const std::string& content) {
            std::ofstream out(path, std::ios::trunc);
            out << content;
        }

        std::string stat1 = "cpu  10500 200 3000 90000 400 0 50 0 0 0\n"
                            "cpu0 5250 100 1500 45000 200 0 25 0 0 0\n"
                            "cpu1 5250 100 1500 45000 200 0 25 0 0 0\n"
                            "intr 48213 12 12 12 12 12 12 12 12\n"
                            "ctxt 1000000\n"
                            "btime 1700000000\n"
                            "processes 5000\n"
                            "procs_running 3\n"
                            "procs_blocked 1\n"
                            "softirq 20000 0 5000 10 3000 200 0 40 6000 0 5750\n";
        std::string stat2 = "cpu  10500 200 3000 90000 400 0 50 0 0 0\n"
                            "cpu0 5250 100 1500 45000 200 0 25 0 0 0\n"
                            "cpu1 5250 100 1500 45000 200 0 25 0 0 0\n"
                            "intr 48213 12 12 12 12 12 12 12 12\n"
                            "ctxt 1050000\n"
                            "btime 1700000000\n"
                            "processes 5300\n"
                            "procs_running 7\n"
                            "procs_blocked 2\n"
                            "softirq 20000 0 5000 10 3000 200 0 40 6000 0 5750\n";
        std::string stat3 = "cpu  10500 200 3000 90000 400 0 50 0 0 0\n"
                            "cpu0 5250 100 1500 45000 200 0 25 0 0 0\n"
                            "cpu1 5250 100 1500 45000 200 0 25 0 0 0\n"
                            "intr 48213 12 12 12 12 12 12 12 12\n"
                            "ctxt 1100000\n"
                            "btime 1700000000\n"
                            "processes 5600\n"
                            "procs_running 7\n"
                            "procs_blocked 2\n"
                            "softirq 20000 0 5000 10 3000 200 0 40 6000 0 5750\n";
        std::string stat_no_ctxt = "cpu  10500 200 3000 90000 400 0 50 0 0 0\n"
                                   "cpu0 5250 100 1500 45000 200 0 25 0 0 0\n"
                                   "cpu1 5250 100 1500 45000 200 0 25 0 0 0\n"
                                   "intr 48213 12 12 12 12 12 12 12 12\n"
                                   "btime 1700000000\n"
                                   "processes 5300\n"
                                   "procs_running 7\n"
                                   "procs_blocked 2\n"
                                   "softirq 20000 0 5000 10 3000 200 0 40 6000 0 5750\n";
        std::string vmstat1 = "nr_free_pages 812345\n"
                              "nr_zone_inactive_anon 45678\n"
                              "nr_zone_active_anon 123456\n"
                              "pgpgin 9876543\n"
                              "pgpgout 5432109\n"
                              "pswpin 10000\n"
                              "pswpout 20000\n"
                              "pgalloc_normal 44556677\n"
                              "pgfree 55667788\n"
                              "pgfault 2000000\n"
                              "pgmajfault 800\n"
                              "pgscan_kswapd 1234\n"
                              "oom_kill 2\n"
                              "thp_fault_alloc 321\n";
        std::string vmstat2 = "nr_free_pages 812345\n"
                              "nr_zone_inactive_anon 45678\n"
                              "nr_zone_active_anon 123456\n"
                              "pgpgin 9876543\n"
                              "pgpgout 5432109\n"
                              "pswpin 11000\n"
                              "pswpout 22000\n"
                              "pgalloc_normal 44556677\n"
                              "pgfree 55667788\n"
                              "pgfault 2020000\n"
                              "pgmajfault 840\n"
                              "pgscan_kswapd 1234\n"
                              "oom_kill 5\n"
                              "thp_fault_alloc 321\n";
        std::string vmstat1_no_oom = "nr_free_pages 812345\n"
                                     "nr_zone_inactive_anon 45678\n"
                                     "nr_zone_active_anon 123456\n"
                                     "pgpgin 9876543\n"
                                     "pgpgout 5432109\n"
                                     "pswpin 10000\n"
                                     "pswpout 20000\n"
                                     "pgalloc_normal 44556677\n"
                                     "pgfree 55667788\n"
                                     "pgfault 2000000\n"
                                     "pgmajfault 800\n"
                                     "pgscan_kswapd 1234\n"
                                     "thp_fault_alloc 321\n";
        std::string vmstat2_no_oom = "nr_free_pages 812345\n"
                                     "nr_zone_inactive_anon 45678\n"
                                     "nr_zone_active_anon 123456\n"
                                     "pgpgin 9876543\n"
                                     "pgpgout 5432109\n"
                                     "pswpin 11000\n"
                                     "pswpout 22000\n"
                                     "pgalloc_normal 44556677\n"
                                     "pgfree 55667788\n"
                                     "pgfault 2020000\n"
                                     "pgmajfault 840\n"
                                     "pgscan_kswapd 1234\n"
                                     "thp_fault_alloc 321\n";
    };

    // missing either file should fail init, and collect after a failed init should fail too
    TEST_F(SystemMetricsCollectorTest, InitializeFailsWithMissingFiles) {
        SystemMetricsCollector no_stat("invalid_file", vmstat_path);
        EXPECT_FALSE(no_stat.initialize());
        EXPECT_FALSE(no_stat.collect());

        SystemMetricsCollector no_vmstat(stat_path, "invalid_file");
        EXPECT_FALSE(no_vmstat.initialize());
    }

    // checks every rate, gauge and the oom total against known deltas
    TEST_F(SystemMetricsCollectorTest, CollectComputesCorrectValues) {
        SystemMetricsCollector collector(stat_path, vmstat_path);
        MetricSnapshot snap;

        ASSERT_TRUE(collector.initialize());
        auto t0 = std::chrono::steady_clock::now();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        write_fixture(stat_path, stat2);
        write_fixture(vmstat_path, vmstat2);

        ASSERT_TRUE(collector.collect());
        auto t1 = std::chrono::steady_clock::now();
        collector.fill_snapshot(snap);

        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        double page_size = static_cast<double>(sysconf(_SC_PAGESIZE));

        // rates, 5% tolerance since sleep time isnt exact
        EXPECT_NEAR(snap.system_metrics.context_switches_per_sec, 50000 / elapsed, 50000 / elapsed * 0.05);
        EXPECT_NEAR(snap.system_metrics.forks_per_sec, 300 / elapsed, 300 / elapsed * 0.05);
        EXPECT_NEAR(snap.system_metrics.page_faults_per_sec, 20000 / elapsed, 20000 / elapsed * 0.05);
        EXPECT_NEAR(snap.system_metrics.major_page_faults_per_sec, 40 / elapsed, 40 / elapsed * 0.05);
        EXPECT_NEAR(snap.system_metrics.swap_in_bytes_per_sec, 1000 * page_size / elapsed, 1000 * page_size / elapsed * 0.05);
        EXPECT_NEAR(snap.system_metrics.swap_out_bytes_per_sec, 2000 * page_size / elapsed, 2000 * page_size / elapsed * 0.05);

        // gauges and oom are raw values, not deltas
        EXPECT_EQ(snap.system_metrics.procs_running, 7u);
        EXPECT_EQ(snap.system_metrics.procs_blocked, 2u);
        EXPECT_EQ(snap.system_metrics.oom_kills_total, 5u);
        EXPECT_EQ(snap.system_metrics.system_calls_per_sec, 0u);
    }

    // older kernels dont have oom_kill, everything else should still work
    TEST_F(SystemMetricsCollectorTest, MissingOomKillStillWorks) {
        write_fixture(vmstat_path, vmstat1_no_oom);
        SystemMetricsCollector collector(stat_path, vmstat_path);
        MetricSnapshot snap;

        ASSERT_TRUE(collector.initialize());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        write_fixture(stat_path, stat2);
        write_fixture(vmstat_path, vmstat2_no_oom);

        ASSERT_TRUE(collector.collect());
        collector.fill_snapshot(snap);

        EXPECT_EQ(snap.system_metrics.oom_kills_total, 0u);
        EXPECT_GT(snap.system_metrics.page_faults_per_sec, 0u);
    }

    // a bad read should skip one tick and keep old values, not break the collector for good
    TEST_F(SystemMetricsCollectorTest, CollectSkipsBadTickThenRecovers) {
        SystemMetricsCollector collector(stat_path, vmstat_path);
        MetricSnapshot snap;

        ASSERT_TRUE(collector.initialize());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        write_fixture(stat_path, stat2);
        write_fixture(vmstat_path, vmstat2);

        ASSERT_TRUE(collector.collect());
        collector.fill_snapshot(snap);
        uint64_t prev_ctxt = snap.system_metrics.context_switches_per_sec;

        write_fixture(stat_path, stat_no_ctxt);
        EXPECT_FALSE(collector.collect());
        collector.fill_snapshot(snap);
        EXPECT_EQ(snap.system_metrics.context_switches_per_sec, prev_ctxt);

        write_fixture(stat_path, stat3);
        EXPECT_TRUE(collector.collect());
    }

} // namespace pulsedb
