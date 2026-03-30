#pragma once
#include <string>
#include <fstream>
#include <vector>
#include <cstdint>

#include "Types.h"
#include "WalManager.h"

namespace pulsedb {
    class PulseFileWriter {
    public:
        explicit PulseFileWriter(const std::string& filepath, MetricType type, const std::string& metric_name, const std::filesystem::path& wal_path);
        ~PulseFileWriter();

        bool open();
        void close();
        bool append(const MetricReading& reading);
        bool flush();

        size_t pending_count() const { return m_chunk_buffer.size(); }
    private:
        void write_file_header();
        void write_blank_index();
        bool compress_and_write_chunk();

        std::string m_filepath;
        std::ofstream m_file;
        std::vector<MetricReading> m_chunk_buffer;

        MetricType m_metric_type;
        std::string m_metric_name;
        uint32_t m_chunk_count = 0;
        WalManager m_wal;

        static constexpr size_t CHUNK_SIZE = 60;
        static constexpr uint32_t CHUNK_INDEX_OFFSET = 64;
        static constexpr uint32_t MAX_CHUNKS_PER_DAY = 1440;
        static constexpr uint32_t INDEX_ENTRY_SIZE = 16;
        static constexpr uint32_t DATA_START_OFFSET = CHUNK_INDEX_OFFSET + (MAX_CHUNKS_PER_DAY * INDEX_ENTRY_SIZE);
    };
}