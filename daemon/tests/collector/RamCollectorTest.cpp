#include <gtest/gtest.h>
#include "collector/RamCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {

    TEST(RamCollectorTest, InitializeSucceeds) {
        RamCollector ram_collector;
        EXPECT_TRUE(ram_collector.initialize());
    }

    TEST(RamCollectorTest, CollectAndFillSnapshots) { 
        RamCollector ram_collector;
        MetricSnapshot snap;

        ram_collector.initialize();
        ram_collector.collect();
        ram_collector.fill_snapshot(snap);

        EXPECT_GT(snap.ram_total_bytes, 0);
        EXPECT_LE(snap.ram_available_bytes, snap.ram_total_bytes);
        EXPECT_LE(snap.ram_used_bytes, snap.ram_total_bytes);
        EXPECT_EQ((snap.ram_used_bytes + snap.ram_available_bytes), snap.ram_total_bytes);

    }

}