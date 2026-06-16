add_executable(pulsedb_tests
    # Source files (no main.cpp)
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

    # Test files
    "tests/storage/StorageEngineTest.cpp"
    "tests/storage/WalManagerTest.cpp"
    "tests/storage/PulseFileReaderTest.cpp"
    "tests/storage/PulseFileWriterTest.cpp"
    "tests/storage/DownsamplerTest.cpp"
    "tests/storage/RetentionManagerTest.cpp"

    "tests/collector/RamCollectorTest.cpp"
    "tests/collector/CpuCollectorTest.cpp"
    "tests/collector/DiskCollectorTest.cpp"
    "tests/collector/NetworkCollectorTest.cpp"
    "tests/collector/ProcessCollectorTest.cpp"
    "tests/collector/SystemMetricsAndPowerCollectorTest.cpp"

    "tests/queue/SpscQueueTest.cpp"
    "tests/queue/RingBufferTest.cpp"
)

target_compile_definitions(pulsedb_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
set_property(TARGET pulsedb_tests PROPERTY CXX_STANDARD 20)
target_include_directories(pulsedb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pulsedb_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    boost::boost
    LZ4::lz4_static
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
    xxHash::xxhash
    pdh
    iphlpapi
)

include(GoogleTest)
gtest_discover_tests(pulsedb_tests)