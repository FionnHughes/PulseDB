#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "collector/DiskCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {
      
    // ensuring a successful initialization
    TEST(DiskCollectorTest, InitializeSucceeds) {
        DiskCollector disk_collector;
        EXPECT_TRUE(disk_collector.initialize());
    }

    // checks we got at least one disk and all fields look valid
    TEST(DiskCollectorTest, CollectAndFillSnapshots) {
        DiskCollector disk_collector;
        MetricSnapshot snap;

        disk_collector.initialize();
        // PDH counters need at least two collections to return values so wait first
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        disk_collector.collect();
        disk_collector.fill_snapshot(snap);

        //ensuring there is at least 1 disk, may want to change
        EXPECT_GT(snap.disks.size(), 0u);
        for (const auto& d : snap.disks) {
            EXPECT_FALSE(d.device_name.empty());
            //greater or equal to as disk can be fully idle
            EXPECT_GE(d.utilization_percent, 0.0f);
            EXPECT_LE(d.utilization_percent, 100.0f);
        }
    }
}