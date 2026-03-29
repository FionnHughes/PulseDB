add_executable(PulseDB 
"PulseDB.cpp" 
"PulseDB.h" 
"storage/PulseFileWriter.h" 
"storage/PulseFileWriter.cpp" 
"storage/PulseFileReader.cpp" 
"storage/PulseFileReader.h" 
"storage/Types.h" 
"storage/StorageEngine.cpp" 
"storage/StorageEngine.h")
set_property(TARGET PulseDB PROPERTY CXX_STANDARD 20)
target_include_directories(PulseDB PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PulseDB PRIVATE
    boost::boost
    LZ4::lz4_static
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
)

enable_testing()

add_executable(pulsedb_tests 
    "tests/StorageEngineTest.cpp"
    "tests/PulseFileWriterTest.cpp" 
    "tests/PulseFileReaderTest.cpp"
    "storage/PulseFileWriter.h" 
    "storage/PulseFileWriter.cpp" 
    "storage/PulseFileReader.h"
    "storage/PulseFileReader.cpp" 
    "storage/Types.h" 
    "storage/StorageEngine.cpp" 
    "storage/StorageEngine.h" )
set_property(TARGET pulsedb_tests PROPERTY CXX_STANDARD 20)
target_include_directories(pulsedb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pulsedb_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    boost::boost
    LZ4::lz4_static
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
)

include(GoogleTest)
gtest_discover_tests(pulsedb_tests)