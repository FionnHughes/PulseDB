# CMakeList.txt : CMake project for PulseDB, include source and define
# project specific logic here.
#
add_executable (PulseDB "PulseDB.cpp" "PulseDB.h")

set_property(TARGET PulseDB PROPERTY CXX_STANDARD 20)

target_link_libraries(PulseDB PRIVATE
    boost::boost
    LZ4::lz4_static
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
    GTest::gtest
)
