#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "collector/CpuCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {

	TEST(CpuCollectorTest, InitializeSucceeds) {
		CpuCollector collector;
		EXPECT_TRUE(collector.initialize());
	}

	TEST(CpuCollectorTest, CollectReturnsTrueAfterInit) {
		CpuCollector collector;
		collector.initialize();

		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		EXPECT_TRUE(collector.collect());
	}

	TEST(CpuCollectorTest, FillSnapshotProducesValidRanges) {
		CpuCollector collector;
		MetricSnapshot snap;

		collector.initialize();

		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		collector.collect();
		collector.fill_snapshot(snap);

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