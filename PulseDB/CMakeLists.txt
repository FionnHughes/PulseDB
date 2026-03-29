add_executable(PulseDB "PulseDB.cpp" "PulseDB.h" "storage/PulseFileWriter.h" "storage/PulseFileWriter.cpp" "storage/PulseFileReader.cpp" "storage/PulseFileReader.h" "tests/PulseFileReaderTest.cpp" "storage/Types.h")
set_property(TARGET PulseDB PROPERTY CXX_STANDARD 20)
target_include_directories(PulseDB PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PulseDB PRIVATE
    boost::boost
    LZ4::lz4_static
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
)

enable_testing()

add_executable(pulsedb_tests "tests/PulseFileWriterTest.cpp" "storage/PulseFileWriter.h" "storage/PulseFileWriter.cpp" "storage/PulseFileReader.cpp" "storage/PulseFileReader.h" "tests/PulseFileReaderTest.cpp" "storage/Types.h")
set_property(TARGET pulsedb_tests PROPERTY CXX_STANDARD 20)
target_include_directories(pulsedb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pulsedb_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(pulsedb_tests)