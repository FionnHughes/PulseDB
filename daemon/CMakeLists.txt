add_executable(pulsedb_daemon
    "src/main.cpp"

    "src/storage/PulseFileWriter.cpp"
    "src/storage/PulseFileReader.cpp"
    "src/storage/StorageEngine.cpp"
    "src/storage/WalManager.cpp"
    "src/storage/Downsampler.cpp"
    "src/storage/RetentionManager.cpp"

    "src/collector/RamCollector.cpp"
    "src/collector/CpuCollector.cpp"
    "src/collector/PdhWrapper.cpp"
    "src/collector/DiskCollector.cpp"
    "src/collector/NetworkCollector.cpp"
    "src/collector/ProcessCollector.cpp"
    "src/collector/SystemMetricsCollector.cpp"
    "src/collector/PowerCollector.cpp"
    "src/collector/CollectorScheduler.cpp"

    "src/common/Utils.cpp"
    "src/common/Config.cpp"

    "src/api/ApiServer.cpp"
    "src/api/websocket/LiveFeedHandler.cpp"

    "src/alerts/AlertEngine.cpp"
    "src/alerts/MetricExtractor.cpp"
)

target_compile_definitions(pulsedb_daemon PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
set_property(TARGET pulsedb_daemon PROPERTY CXX_STANDARD 20)
target_include_directories(pulsedb_daemon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pulsedb_daemon PRIVATE
    boost::boost
    LZ4::lz4_static
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
    xxHash::xxhash
    Drogon::Drogon
    pdh
    iphlpapi
)