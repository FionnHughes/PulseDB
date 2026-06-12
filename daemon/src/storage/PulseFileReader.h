#pragma once
#include <fstream>
#include <vector>

#include "Types.h"

namespace pulsedb {
    class PulseFileReader {
    public:
        explicit PulseFileReader(const std::string& filepath);
        bool open();
        void close();
        std::vector<MetricReading> query(int64_t from_ms, int64_t to_ms);

    private:
        std::string m_filepath;
        std::ifstream m_file;
        FileHeader m_header;
        std::vector<ChunkIndexEntry> m_index;
    };
}