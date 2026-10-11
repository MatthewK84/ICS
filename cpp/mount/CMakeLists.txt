# ics::mount (ICS-033): the client for PlaneWave Interface 4, which runs each
# station's mount, holding every goto and rate to the axis limits, and the
# loop that sends rate demands at 20 to 50 Hz. See cpp/README.md.
find_package(CURL REQUIRED)

add_library(ics_mount STATIC
  src/limits.cpp
  src/pwi4_client.cpp
  src/pwi4_status.cpp
  src/rate_loop.cpp)
add_library(ics::mount ALIAS ics_mount)
target_include_directories(ics_mount PUBLIC include)
target_link_libraries(ics_mount PUBLIC ics::common ics::logging PRIVATE CURL::libcurl)

ics_add_gtest(mount_test
  SOURCES
    test/bench_test.cpp
    test/limits_test.cpp
    test/pwi4_client_test.cpp
    test/pwi4_status_test.cpp
    test/rate_loop_test.cpp
  LIBRARIES ics::mount)
target_include_directories(mount_test PRIVATE test)

# The status comes off the station network, so the parser is fuzzed with any
# text.
ics_add_fuzzer(status_fuzz SOURCES fuzz/status_fuzz.cpp LIBRARIES ics::mount
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
