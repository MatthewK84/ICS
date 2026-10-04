# ics::cot (ICS-022): the Cursor on Target adapter. It reads the CoT XML events
# in the UDP datagrams a TAP port captures, with pugixml, and turns their
# positions into ics.v1.PliRecord.
find_package(pugixml CONFIG REQUIRED)

add_library(ics_cot STATIC
  src/adapter.cpp
  src/event.cpp
  src/time.cpp
  src/uncertainty.cpp)
add_library(ics::cot ALIAS ics_cot)
target_include_directories(ics_cot PUBLIC include)
target_link_libraries(ics_cot PUBLIC ics::common ics::frames ics::proto PRIVATE pugixml::pugixml)

ics_add_gtest(cot_test
  SOURCES
    test/adapter_test.cpp
    test/event_test.cpp
    test/samples_test.cpp
    test/time_test.cpp
    test/uncertainty_test.cpp
  LIBRARIES ics::capture ics::cot)
target_include_directories(cot_test PRIVATE test ${PROJECT_SOURCE_DIR}/capture/test ${PROJECT_SOURCE_DIR}/timing/test)
target_compile_definitions(cot_test PRIVATE ICS_COT_SAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/test/samples")

# CoT arrives from other systems as XML, so the reader and adapter are fuzzed
# end to end (ICS-008). The corpus holds the sample feeds.
ics_add_fuzzer(cot_fuzz SOURCES fuzz/cot_fuzz.cpp LIBRARIES ics::capture ics::cot
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
