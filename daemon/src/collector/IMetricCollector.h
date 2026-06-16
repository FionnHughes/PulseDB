#pragma once

#include <string>

namespace pulsedb {

    struct MetricSnapshot;

    // interface all collectors implement to initialize once on startup, collect every second, fill_snapshot copies the result
    class IMetricCollector {
    public:
        virtual ~IMetricCollector() = default;

        virtual std::string name() const = 0;

        // called once to set up counters, allocate buffers, seed any previous-value state needed for deltas
        virtual bool initialize() = 0;

        // called every tick which samples the metric and caches the result locally
        virtual bool collect() = 0;

        // copies the cached result into the shared snapshot after all collectors have finished collect()
        virtual void fill_snapshot(MetricSnapshot& snap) const = 0;
        virtual void shutdown() = 0;
    };

}