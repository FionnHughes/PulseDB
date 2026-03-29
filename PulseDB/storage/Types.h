#pragma once
#include <string>
#include <vector>
#include <cstdint>

static constexpr uint8_t PULSE_MAGIC[4] = { 0x50, 0x55, 0x4C, 0x53 };
static constexpr uint16_t PULSE_VERSION = 1;

namespace pulsedb {
    struct MetricReading {
        int64_t timestamp_ms;
        double value;
    };
}

enum class MetricType : uint8_t {
    cpu_total = 0x01,
    cpu_core = 0x02,
    ram_used = 0x03,
    ram_available = 0x04,
    disk_read = 0x05,
    disk_write = 0x06,
    net_in = 0x07,
    net_out = 0x08
};

#pragma pack(push, 1)
struct FileHeader {
    char     magic[4];           // "PULS"
    uint16_t version;            // 1
    uint8_t  metric_type_id;     // 0x01, 0x02 etc
    uint8_t  reserved;           // 0x00
    int64_t  creation_ts;        // unix ms
    int64_t  day_start_ts;       // midnight unix ms
    uint32_t chunk_count;        // starts at 0
    uint32_t chunk_index_offset; // 64
    uint32_t readings_per_chunk; // 60
    uint32_t collection_interval_ms; // 1000
    char     metric_name[24];    // null-padded
};

struct ChunkIndexEntry {
    int64_t  chunk_start_ts;   // unix ms of first reading
    uint32_t byte_offset;      // where in the file this chunk lives
    uint32_t compressed_size;  // how many bytes the compressed blob is
};

struct ChunkHeader {
    int64_t  base_timestamp_ms;
    uint16_t reading_count;
    uint16_t reserved;
    uint32_t uncompressed_size;
};
#pragma pack(pop)
