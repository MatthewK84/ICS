# ics::lattice (ICS-023): the Lattice adapter. It reads the entity events
# Lattice streams as server-sent events over HTTPS, with libcurl, and turns
# their positions into ics.v1.PliRecord.
find_package(CURL REQUIRED)

add_library(ics_lattice STATIC
  src/adapter.cpp
  src/event.cpp
  src/sse.cpp
  src/stream_client.cpp
  src/token.cpp)
add_library(ics::lattice ALIAS ics_lattice)
target_include_directories(ics_lattice PUBLIC include)
target_link_libraries(ics_lattice PUBLIC ics::common ics::frames ics::proto PRIVATE ics::cot CURL::libcurl)

ics_add_gtest(lattice_test
  SOURCES
    test/adapter_test.cpp
    test/event_test.cpp
    test/samples_test.cpp
    test/sse_test.cpp
    test/stream_client_test.cpp
    test/token_test.cpp
  LIBRARIES ics::lattice)
target_include_directories(lattice_test PRIVATE test)
target_compile_definitions(lattice_test PRIVATE ICS_LATTICE_SAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/test/samples")

# Lattice is a peer system, so its stream is fuzzed from the SSE reader to the
# record (ICS-008). The corpus holds synthetic streams and events.
ics_add_fuzzer(lattice_fuzz SOURCES fuzz/lattice_fuzz.cpp LIBRARIES ics::lattice
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
