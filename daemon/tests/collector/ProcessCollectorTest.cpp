#include <gtest/gtest.h>
#include <windows.h>
#include <thread>
#include <chrono>

#include "collector/ProcessCollector.h"
#include "collector/MetricSnapshot.h"

namespace pulsedb {

	TEST(ProcessCollector, InitializeSucceeds) {
		ProcessCollector collector;
		EXPECT_TRUE(collector.initialize());
	}

	TEST(ProcessCollector, CollectReturnsTrue) {
		ProcessCollector collector;
		collector.initialize();
		EXPECT_TRUE(collector.collect());
	}

	// checks the list is sorted descending by cpu_percent and has at most 25 entries
	TEST(ProcessCollector, TopProcessesValidAfterCollect) {
		ProcessCollector collector;
		collector.initialize();
		collector.collect();

		pulsedb::MetricSnapshot snap;
		collector.fill_snapshot(snap);
		EXPECT_FALSE(snap.top_processes.empty());
		EXPECT_LE(snap.top_processes.size(), 25);
		for (size_t i = 1; i < snap.top_processes.size(); i++) {
			EXPECT_LE(snap.top_processes[i].cpu_percent, snap.top_processes[i - 1].cpu_percent);
		}
	}

	// all cpu percentages should be in range 0 - 100
	TEST(ProcessCollector, AllProcessesHaveValidMetrics) {
		ProcessCollector collector;
		collector.initialize();
		collector.collect();

		pulsedb::MetricSnapshot snap;
		collector.fill_snapshot(snap);
		for (int i = 0; i < snap.top_processes.size(); i++) {
			EXPECT_GE(snap.top_processes[i].ram_bytes, 0u);

			EXPECT_GE(snap.top_processes[i].cpu_percent, 0.0f);
			EXPECT_LE(snap.top_processes[i].cpu_percent, 100.0f);
		}
	}

	// pulsedb_pid should match the current process ID
	TEST(ProcessCollector, SelfMonitoringFieldsAreValid) {
		ProcessCollector collector;
		collector.initialize();
		collector.collect();

		pulsedb::MetricSnapshot snap;
		collector.fill_snapshot(snap);
		
		EXPECT_EQ(snap.pulsedb_pid, GetCurrentProcessId());
		EXPECT_GE(snap.pulsedb_ram_bytes, 0u);
	}

	// first collect always returns 0% for everything as theres nothing to compute against so need two collects for real values
	TEST(ProcessCollector, SecondCollectHasNonZeroCpu) {
		ProcessCollector collector;
		collector.initialize();
		collector.collect();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		collector.collect();

		pulsedb::MetricSnapshot snap;
		collector.fill_snapshot(snap);

		EXPECT_GT(snap.top_processes[0].cpu_percent, 0.0f);
	}
}