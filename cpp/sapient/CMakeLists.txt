# ics::sapient (ICS-024): the SAPIENT adapter. It reads the BSI Flex 335 v2.0
# messages a SAPIENT node sends over TCP, framed by length, and turns the
# positions in their detection reports into ics.v1.PliRecord. The messages are
# Dstl's protobuf definitions (proto/third_party/sapient_msg), generated into
# ics::proto.
add_library(ics_sapient STATIC
  src/adapter.cpp
  src/location.cpp
  src/registration.cpp
  src/stream.cpp)
add_library(ics::sapient ALIAS ics_sapient)
target_include_directories(ics_sapient PUBLIC include)
target_link_libraries(ics_sapient PUBLIC ics::common ics::frames ics::proto)

ics_add_gtest(sapient_test
  SOURCES
    test/adapter_test.cpp
    test/location_test.cpp
    test/registration_test.cpp
    test/samples_test.cpp
    test/stream_test.cpp
  LIBRARIES ics::sapient)
target_include_directories(sapient_test PRIVATE test)
target_compile_definitions(sapient_test PRIVATE
  ICS_EGM96_PATH="${ICS_GEOID_DIR}/egm96-5.pgm"
  ICS_SAPIENT_SAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/test/samples")

# SAPIENT nodes are peer systems, so their stream is fuzzed from the framing
# to the record (ICS-008). The corpus holds the samples, framed.
ics_add_fuzzer(sapient_fuzz SOURCES fuzz/sapient_fuzz.cpp LIBRARIES ics::sapient
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
