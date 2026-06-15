#include <gtest/gtest.h>
#include <windows.h>

#include "collector/SystemMetricsCollector.h"
#include "collector/PowerCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {
	TEST(SystemMetricsCollector, InitializeSucceeds) {
		SystemMetricsCollector collector;
		EXPECT_TRUE(collector.initialize());
	}

	TEST(SystemMetricsCollector, CollectReturnsTrue) {
		SystemMetricsCollector collector;
		collector.initialize();
		EXPECT_TRUE(collector.collect());
	}

	TEST(SystemMetricsCollector, SecondCollectSucceeds) {
		SystemMetricsCollector collector;
		collector.initialize();
		collector.collect();
		EXPECT_TRUE(collector.collect());

		pulsedb::MetricSnapshot snap;
		collector.fill_snapshot(snap);
	}

	TEST(PowerCollector, InitializeSucceeds) {
		PowerCollector collector;
		EXPECT_TRUE(collector.initialize());
	}

	TEST(PowerCollector, CollectReturnsTrue) {
		PowerCollector collector;
		collector.initialize();
		EXPECT_TRUE(collector.collect());
	}

	TEST(PowerCollector, BatteryPercentInValidRange) {
		PowerCollector collector;
		collector.initialize();
		collector.collect();

		pulsedb::MetricSnapshot snap;	
		collector.fill_snapshot(snap);
		EXPECT_TRUE((snap.power.battery_percent <= 100 && snap.power.battery_percent >= 0) || snap.power.battery_percent == 255);
	}
}