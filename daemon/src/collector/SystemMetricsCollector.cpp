#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <winternl.h>

#include "SystemMetricsCollector.h"

//this is the full SYSTEM_PERFORMANCE_INFORMATION for 64 bit Win
//winternl.h only shows a few fields, minimal amount, so i have it defined here to access PageFaultCount, ContextSwitches + SystemCalls
//order and offsets from geoffchappell.com docs.
struct FULL_SYSTEM_PERFORMANCE_INFORMATION {
    LARGE_INTEGER IdleProcessTime;
    LARGE_INTEGER IoReadTransferCount;
    LARGE_INTEGER IoWriteTransferCount;
    LARGE_INTEGER IoOtherTransferCount;
    ULONG         IoReadOperationCount;
    ULONG         IoWriteOperationCount;
    ULONG         IoOtherOperationCount;
    ULONG         AvailablePages;
    ULONG         CommittedPages;
    ULONG         CommitLimit;
    ULONG         PeakCommitment;
    ULONG         PageFaultCount;
    ULONG         CopyOnWriteCount;
    ULONG         TransitionCount;
    ULONG         CacheTransitionCount;
    ULONG         DemandZeroCount;
    ULONG         PageReadCount;
    ULONG         PageReadIoCount;
    ULONG         CacheReadCount;
    ULONG         CacheIoCount;
    ULONG         DirtyPagesWriteCount;
    ULONG         DirtyWriteIoCount;
    ULONG         MappedPagesWriteCount;
    ULONG         MappedWriteIoCount;
    ULONG         PagedPoolPages;
    ULONG         NonPagedPoolPages;
    ULONG         PagedPoolAllocs;
    ULONG         PagedPoolFrees;
    ULONG         NonPagedPoolAllocs;
    ULONG         NonPagedPoolFrees;
    ULONG         FreeSystemPtes;
    ULONG         ResidentSystemCodePage;
    ULONG         TotalSystemDriverPages;
    ULONG         TotalSystemCodePages;
    ULONG         NonPagedPoolLookasideHits;
    ULONG         PagedPoolLookasideHits;
    ULONG         AvailablePagedPoolPages;
    ULONG         ResidentSystemCachePage;
    ULONG         ResidentPagedPoolPage;
    ULONG         ResidentSystemDriverPage;
    ULONG         CcFastReadNoWait;
    ULONG         CcFastReadWait;
    ULONG         CcFastReadResourceMiss;
    ULONG         CcFastReadNotPossible;
    ULONG         CcFastMdlReadNoWait;
    ULONG         CcFastMdlReadWait;
    ULONG         CcFastMdlReadResourceMiss;
    ULONG         CcFastMdlReadNotPossible;
    ULONG         CcMapDataNoWait;
    ULONG         CcMapDataWait;
    ULONG         CcMapDataNoWaitMiss;
    ULONG         CcMapDataWaitMiss;
    ULONG         CcPinMappedDataCount;
    ULONG         CcPinReadNoWait;
    ULONG         CcPinReadWait;
    ULONG         CcPinReadNoWaitMiss;
    ULONG         CcPinReadWaitMiss;
    ULONG         CcCopyReadNoWait;
    ULONG         CcCopyReadWait;
    ULONG         CcCopyReadNoWaitMiss;
    ULONG         CcCopyReadWaitMiss;
    ULONG         CcMdlReadNoWait;
    ULONG         CcMdlReadWait;
    ULONG         CcMdlReadNoWaitMiss;
    ULONG         CcMdlReadWaitMiss;
    ULONG         CcReadAheadIos;
    ULONG         CcLazyWriteIos;
    ULONG         CcLazyWritePages;
    ULONG         CcDataFlushes;
    ULONG         CcDataPages;
    ULONG         ContextSwitches;
    ULONG         FirstLevelTbFills;
    ULONG         SecondLevelTbFills;
    ULONG         SystemCalls;
};

static_assert(offsetof(FULL_SYSTEM_PERFORMANCE_INFORMATION, PageFaultCount) == 0x3C);
static_assert(offsetof(FULL_SYSTEM_PERFORMANCE_INFORMATION, ContextSwitches) == 0x0128);
static_assert(offsetof(FULL_SYSTEM_PERFORMANCE_INFORMATION, SystemCalls) == 0x0134);
namespace pulsedb {
    //still returning the name 
	std::string SystemMetricsCollector::name() const {
        return "system_metrics";
	}

	bool SystemMetricsCollector::initialize() {
        //querying system performance
		auto fn = pulsedb::NtdllHelper::get().query_fn();
        if (!fn) return false;

		ULONG bytes_returned = 0;
        FULL_SYSTEM_PERFORMANCE_INFORMATION info{};

		NTSTATUS status = fn(
            SystemPerformanceInformation,
			&info,
            static_cast<ULONG>(sizeof(info)),
			&bytes_returned
		);

		if (status != STATUS_SUCCESS) {
			return false;
		}
        //initilizing previous values
        m_prev_context_switches = info.ContextSwitches;
        m_prev_system_calls = info.SystemCalls;
        m_prev_page_faults = info.PageFaultCount;

        return true;
    }

    bool SystemMetricsCollector::collect() {
        //querying again
        auto fn = pulsedb::NtdllHelper::get().query_fn();
        if (!fn) return false;

        ULONG bytes_returned = 0;
        FULL_SYSTEM_PERFORMANCE_INFORMATION info{};

        NTSTATUS status = fn(
            SystemPerformanceInformation,
            &info,
            static_cast<ULONG>(sizeof(info)),
            &bytes_returned
        );

        if (status != STATUS_SUCCESS) {
            return false;
        }

        //computing per second deltas
        m_context_switches_per_sec = info.ContextSwitches - m_prev_context_switches;
        m_system_calls_per_sec = info.SystemCalls - m_prev_system_calls;
        m_page_faults_per_sec = info.PageFaultCount - m_prev_page_faults;

        //setting all values again very simple
        m_prev_context_switches = info.ContextSwitches;
        m_prev_system_calls = info.SystemCalls;
        m_prev_page_faults = info.PageFaultCount;

        return true;
    }

    //storing values in snapshot
    void SystemMetricsCollector::fill_snapshot(MetricSnapshot& snap) const {
        snap.system_metrics.context_switches_per_sec = m_context_switches_per_sec;
        snap.system_metrics.system_calls_per_sec = m_system_calls_per_sec;
        snap.system_metrics.page_faults_per_sec = m_page_faults_per_sec;

    }

    //zeroing everything
    void SystemMetricsCollector::shutdown() {
        m_prev_context_switches = 0u;
        m_prev_system_calls = 0u;
        m_prev_page_faults = 0u;

        m_context_switches_per_sec = 0u;
        m_system_calls_per_sec = 0u;
        m_page_faults_per_sec = 0u;
    }
}