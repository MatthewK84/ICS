# ics::mavlink (ICS-021): the MAVLink adapter. It reads the frames of the
# messages ICS uses from the UDP datagrams a TAP port captures, and turns them
# into ics.v1.PliRecord and PliEvent.
add_library(ics_mavlink STATIC
  src/adapter.cpp
  src/datagram.cpp
  src/frame.cpp
  src/messages.cpp
  src/modes.cpp)
add_library(ics::mavlink ALIAS ics_mavlink)
target_include_directories(ics_mavlink PUBLIC include)
target_link_libraries(ics_mavlink PUBLIC ics::common ics::frames ics::proto)

ics_add_gtest(mavlink_test
  SOURCES
    test/adapter_test.cpp
    test/datagram_test.cpp
    test/fixture_test.cpp
    test/frame_test.cpp
    test/messages_test.cpp
    test/modes_test.cpp
  LIBRARIES ics::capture ics::mavlink)
target_include_directories(mavlink_test PRIVATE test)
target_compile_definitions(mavlink_test PRIVATE
  ICS_EGM96_PATH="${ICS_GEOID_DIR}/egm96-5.pgm"
  ICS_MAVLINK_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")

# The adapter parses what a TAP port captures, so it is fuzzed end to end
# (ICS-008). The corpus holds frames the SITL rig's own encoder wrote.
ics_add_fuzzer(mavlink_fuzz SOURCES fuzz/mavlink_fuzz.cpp LIBRARIES ics::mavlink
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
