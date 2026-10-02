#pragma once

#include <string>
#include <vector>

#include "../IMetricCollector.h"
#include "../MetricSnapshot.h"

namespace pulsedb {
    // temporary stub until the real linux process collector exists, api gets an empty list
    class ProcessCollector : public IMetricCollector {
    public:
        std::string name() const override { return "process"; }
        bool initialize() override { return true; }
        bool collect() override { return true; }
        void fill_snapshot(MetricSnapshot&) const override {}
        void shutdown() override {}

        const std::vector<MetricSnapshot::ProcessInfo>& get_all_processes() const { return m_all_processes; }

    private:
        // always empty, only here so get_all_processes has something to return a reference to
        std::vector<MetricSnapshot::ProcessInfo> m_all_processes;
    };
} // namespace pulsedb
