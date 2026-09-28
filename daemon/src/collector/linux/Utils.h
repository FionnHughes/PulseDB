#include <cstdint>

namespace pulsedb {

    inline float compute_percent(uint64_t part_delta, uint64_t total_delta) {
        if (total_delta == 0) {
            return 0.0f;
        }
        return (static_cast<float>(part_delta) / static_cast<float>(total_delta)) * 100.0f;
    }

} // namespace pulsedb
