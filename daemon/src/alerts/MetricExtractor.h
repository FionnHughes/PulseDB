#pragma once

#include <optional>
#include <string>
#include "../collector/MetricSnapshot.h"

namespace pulsedb {

	// pulls one metric's value out of a snapshot by name, returns nullopt if it doesn't exist
	std::optional<double> extract_metric_value(const MetricSnapshot& snap, const std::string& metric_name);

}