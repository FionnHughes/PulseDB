enable_testing()
add_executable(pulsedb_tests
    "tests/storage/PulseFileWriterTest.cpp"
    "tests/storage/PulseFileReaderTest.cpp"
    "tests/storage/StorageEngineTest.cpp"
    "tests/storage/WalManagerTest.cpp"
    "tests/storage/DownsamplerTest.cpp"
    "tests/storage/RetentionManagerTest.cpp"
    "tests/collector/CpuCollectorTest.cpp"
    "tests/collector/DiskCollectorTest.cpp"
    "tests/collector/NetworkCollectorTest.cpp"
    "tests/collector/ProcessCollectorTest.cpp"

    "src/storage/PulseFileWriter.h"
    "src/storage/PulseFileWriter.cpp"
    "src/storage/PulseFileReader.h"
    "src/storage/PulseFileReader.cpp"
    "src/storage/Types.h"
    "src/storage/StorageEngine.h"
    "src/storage/StorageEngine.cpp"
    "src/storage/WalManager.h"
    "src/storage/WalManager.cpp"
    "src/storage/Downsampler.h"
    "src/storage/Downsampler.cpp"
    "src/storage/RetentionManager.h"
    "src/storage/RetentionManager.cpp"

    "src/collector/IMetricCollector.h"
    "src/collector/MetricSnapshot.h"
    "src/collector/NtdllHelper.h"
    "tests/collector/RamCollectorTest.cpp"
    "src/collector/RamCollector.h"
    "src/collector/RamCollector.cpp"
    "src/collector/CpuCollector.h"
    "src/collector/CpuCollector.cpp"
    "src/collector/PdhWrapper.h"
    "src/collector/PdhWrapper.cpp"
    "src/collector/DiskCollector.h"
    "src/collector/DiskCollector.cpp"
    "src/collector/NetworkCollector.h"
    "src/collector/NetworkCollector.cpp"
    "src/collector/ProcessCollector.h"
    "src/collector/ProcessCollector.cpp"

    "src/common/Utils.h"
    "src/common/Utils.cpp" "tests/collector/ProcessCollectorTest.cpp")
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