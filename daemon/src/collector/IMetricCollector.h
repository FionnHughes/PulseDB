#pragma once

#include <string>

namespace pulsedb {

    struct MetricSnapshot;

    class IMetricCollector {
    public:
        virtual ~IMetricCollector() = default;

        virtual std::string name() const = 0;
        virtual bool initialize() = 0;
        virtual bool collect() = 0;
        virtual void fill_snapshot(MetricSnapshot& snap) const = 0;
        virtual void shutdown() = 0;
    };

}