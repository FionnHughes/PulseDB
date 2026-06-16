#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "collector/CpuCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {
	// smoke test as initialize should succeed on any windows machine
	TEST(CpuCollectorTest, InitializeSucceeds) {
		CpuCollector collector;
		EXPECT_TRUE(collector.initialize());
	}

	TEST(CpuCollectorTest, CollectReturnsTrueAfterInit) {
		CpuCollector collector;
		collector.initialize();

		// need some elapsed time between calls so the counter deltas are useful
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		EXPECT_TRUE(collector.collect());
	}

	// all percentages should be in in range of 0 - 100 and the per core vector correct size	
	TEST(CpuCollectorTest, FillSnapshotProducesValidRanges) {
		CpuCollector cpu_collector;
		MetricSnapshot snap;

		cpu_collector.initialize();

		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		cpu_collector.collect();
		cpu_collector.fill_snapshot(snap);

		EXPECT_GE(snap.cpu_total_percent, 0.0f);
		EXPECT_LE(snap.cpu_total_percent, 100.0f);
		EXPECT_GT(snap.cpu_per_core_percent.size(), 0u);

		for (auto v : snap.cpu_per_core_percent) {
			EXPECT_GE(v, 0.0f);
			EXPECT_LE(v, 100.0f);
		}

		EXPECT_EQ(snap.cpu_per_core_frequency_mhz.size(), snap.cpu_per_core_percent.size());
	}

}