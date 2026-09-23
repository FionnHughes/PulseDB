set(PULSEDB_COMMON_SOURCES
    "src/storage/PulseFileWriter.cpp"
    "src/storage/PulseFileReader.cpp"
    "src/storage/StorageEngine.cpp"
    "src/storage/WalManager.cpp"
    "src/storage/Downsampler.cpp"
    "src/storage/RetentionManager.cpp"
)

set(PULSEDB_COMMON_TEST_SOURCES
    "tests/storage/StorageEngineTest.cpp"
    "tests/storage/WalManagerTest.cpp"
    "tests/storage/PulseFileReaderTest.cpp"
    "tests/storage/PulseFileWriterTest.cpp"
    "tests/storage/DownsamplerTest.cpp"
    "tests/storage/RetentionManagerTest.cpp"

    "tests/queue/SpscQueueTest.cpp"
    "tests/queue/RingBufferTest.cpp"
)

set(PULSEDB_WINDOWS_COLLECTOR_SOURCES
    "src/common/Utils.cpp"

    "src/collector/RamCollector.cpp"
    "src/collector/CpuCollector.cpp"
    "src/collector/PdhWrapper.cpp"
    "src/collector/DiskCollector.cpp"
    "src/collector/NetworkCollector.cpp"
    "src/collector/ProcessCollector.cpp"
    "src/collector/SystemMetricsCollector.cpp"
    "src/collector/PowerCollector.cpp"
    "src/collector/CollectorScheduler.cpp"
)

set(PULSEDB_WINDOWS_COLLECTOR_TEST_SOURCES
    "tests/collector/RamCollectorTest.cpp"
    "tests/collector/CpuCollectorTest.cpp"
    "tests/collector/DiskCollectorTest.cpp"
    "tests/collector/NetworkCollectorTest.cpp"
    "tests/collector/ProcessCollectorTest.cpp"
    "tests/collector/SystemMetricsAndPowerCollectorTest.cpp"
)

set(PULSEDB_LINUX_COLLECTOR_SOURCES
    "src/collector/linux/CpuCollector.cpp"
    "src/collector/linux/RamCollector.cpp"
    "src/collector/linux/DiskCollector.cpp"
)

# empty for now will fill in as Linux collector test files get written
set(PULSEDB_LINUX_COLLECTOR_TEST_SOURCES
    "tests/collector/linux/CpuCollectorTest.cpp"
    "tests/collector/linux/RamCollectorTest.cpp"
)


set(PULSEDB_TESTS_SOURCES
    ${PULSEDB_COMMON_SOURCES}
    ${PULSEDB_COMMON_TEST_SOURCES}
)

if (WIN32)
    list(APPEND PULSEDB_TESTS_SOURCES
        ${PULSEDB_WINDOWS_COLLECTOR_SOURCES}
        ${PULSEDB_WINDOWS_COLLECTOR_TEST_SOURCES}
    )
else()
    list(APPEND PULSEDB_TESTS_SOURCES
        ${PULSEDB_LINUX_COLLECTOR_SOURCES}
        ${PULSEDB_LINUX_COLLECTOR_TEST_SOURCES}
    )
endif()

add_executable(pulsedb_tests ${PULSEDB_TESTS_SOURCES})

if (WIN32)
    target_compile_definitions(pulsedb_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

set_property(TARGET pulsedb_tests PROPERTY CXX_STANDARD 20)
target_include_directories(pulsedb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (WIN32)
    target_include_directories(pulsedb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/collector)
else()
    target_include_directories(pulsedb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/collector/linux)
endif()

target_link_libraries(pulsedb_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    boost::boost
    LZ4::lz4_static
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
    xxHash::xxhash
)

if (WIN32)
    target_link_libraries(pulsedb_tests PRIVATE pdh iphlpapi)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(pulsedb_tests PRIVATE Threads::Threads)
endif()

include(GoogleTest)
gtest_discover_tests(pulsedb_tests)

# Windows only for now

if (WIN32)
    add_executable(pulsedb_daemon
        "src/main.cpp"

        ${PULSEDB_COMMON_SOURCES}
        ${PULSEDB_WINDOWS_COLLECTOR_SOURCES}

        "src/common/Config.cpp"

        "src/api/ApiServer.cpp"
        "src/api/websocket/LiveFeedHandler.cpp"

        "src/alerts/AlertEngine.cpp"
        "src/alerts/MetricExtractor.cpp"
    )

    target_compile_definitions(pulsedb_daemon PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
    set_property(TARGET pulsedb_daemon PROPERTY CXX_STANDARD 20)
    target_include_directories(pulsedb_daemon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_include_directories(pulsedb_daemon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/collector)
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
endif()
