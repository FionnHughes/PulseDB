#pragma once
#include <string>
#include <fstream>
#include <vector>
#include <cstdint>

#include "Types.h"
#include "WalManager.h"

namespace pulsedb {
    // writes metric readings to a .pulse binary file doing one file per metric per day
    class PulseFileWriter {
    public:
        explicit PulseFileWriter(const std::string& filepath, MetricType type, const std::string& metric_name, const std::filesystem::path& wal_path);
        ~PulseFileWriter();

        bool open();
        void close();
        bool append(const MetricReading& reading);
        bool flush();

        // the storage engine checks this every append to detect midnight rollover
        int64_t day_start_ts() const { return m_day_start_ts; }

        size_t pending_count() const { return m_chunk_buffer.size(); }
    private:
        void write_file_header();
        void write_blank_index();
        bool compress_and_write_chunk();

        std::string m_filepath;
        std::fstream m_file;
        std::vector<MetricReading> m_chunk_buffer;

        MetricType m_metric_type;
        std::string m_metric_name;
        uint32_t m_chunk_count = 0;
        int64_t m_day_start_ts = 0;

        // WAL for crash recovery - every chunk gets logged here before being written to the .pulse file
        WalManager m_wal;

        // 60 readings per chunk = one minute of data at 1 second collection
        static constexpr size_t CHUNK_SIZE = 60;
        static constexpr uint32_t CHUNK_INDEX_OFFSET = 64;

        // 1440 minutes in a day so the index has one slot pre-allocated for each
        static constexpr uint32_t MAX_CHUNKS_PER_DAY = 1440;
        static constexpr uint32_t INDEX_ENTRY_SIZE = 16;

        // actual data starts at byte 23104 after the header and the full pre allocated index
        static constexpr uint32_t DATA_START_OFFSET = CHUNK_INDEX_OFFSET + (MAX_CHUNKS_PER_DAY * INDEX_ENTRY_SIZE);
    };
}