#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "collector/NetworkCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {

    TEST(NetworkCollectorTest, InitializeSucceeds) {
        NetworkCollector network_collector;
        EXPECT_TRUE(network_collector.initialize());
    }

    // checks we got at least one adapter and the name is not empty
    TEST(NetworkCollectorTest, CollectAndFillSnapshots) {
        NetworkCollector network_collector;
        MetricSnapshot snap;

        network_collector.initialize();
        // give the adapter counters some time so the delta between ticks is useful
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        network_collector.collect();
        network_collector.fill_snapshot(snap);

        EXPECT_GT(snap.network_adapters.size(), 0u);
        for (const auto& a : snap.network_adapters) {
            EXPECT_FALSE(a.adapter_name.empty());
        }
    }
}