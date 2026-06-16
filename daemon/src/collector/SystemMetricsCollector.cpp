#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <winternl.h>

#include "SystemMetricsCollector.h"

//this is the full SYSTEM_PERFORMANCE_INFORMATION for 64 bit Win
//winternl.h only shows a few fields, minimal amount, so i have it defined here to access PageFaultCount, ContextSwitches + SystemCalls
//order and offsets from geoffchappell.com docs.
namespace {
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
}

namespace pulsedb {
    // still returning the name 
	std::string SystemMetricsCollector::name() const {
        return "system_metrics";
	}

    // sets the previous values and the QPC baseline for collect() to delta against
	bool SystemMetricsCollector::initialize() {
        if (!QueryPerformanceFrequency(&m_freq)) return false;

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
        // initilizing previous values
        m_prev_context_switches = info.ContextSwitches;
        m_prev_system_calls = info.SystemCalls;
        m_prev_page_faults = info.PageFaultCount;

        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        m_prev_qpc = qpc.QuadPart;

        return true;
    }

    // queries FULL_SYSTEM_PERFORMANCE_INFORMATION, computes deltas from previous tick and divides by real elapsed time
    bool SystemMetricsCollector::collect() {
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

        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);

        // using QPC for elapsed time because the actual tick interval varies slightly from 1s
        double elapsed = static_cast<double>(qpc.QuadPart - m_prev_qpc) / static_cast<double>(m_freq.QuadPart);
        // shouldn't ever happen but stops division by zero anyway
        if (elapsed <= 0.0) elapsed = 1.0;
        m_prev_qpc = qpc.QuadPart;

        // uint32 subtraction wraps correctly when the counter rolls over
        uint32_t cs_delta = static_cast<uint32_t>(info.ContextSwitches) - m_prev_context_switches;
        uint32_t sc_delta = static_cast<uint32_t>(info.SystemCalls) - m_prev_system_calls;
        uint32_t pf_delta = static_cast<uint32_t>(info.PageFaultCount) - m_prev_page_faults;

        //computing per second deltas, adding proper casting
        m_context_switches_per_sec = static_cast<uint64_t>(cs_delta / elapsed);
        m_system_calls_per_sec = static_cast<uint64_t>(sc_delta / elapsed);
        m_page_faults_per_sec = static_cast<uint64_t>(pf_delta / elapsed);

        // setting all values again but adding casting in fix
        m_prev_context_switches = static_cast<uint32_t>(info.ContextSwitches);
        m_prev_system_calls = static_cast<uint32_t>(info.SystemCalls);
        m_prev_page_faults = static_cast<uint32_t>(info.PageFaultCount);

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