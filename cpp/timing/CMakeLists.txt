# ics::timing (ICS-019): the station's time quality from ptp4l's management
# socket, and the local socket ics-timingd publishes it on. See
# services/timingd for the service.
add_library(ics_timing STATIC
  src/clock_state.cpp
  src/ptp_client.cpp
  src/ptp_management.cpp
  src/publisher.cpp
  src/quality.cpp
  src/unix_socket.cpp)
add_library(ics::timing ALIAS ics_timing)
target_include_directories(ics_timing PUBLIC include)
target_link_libraries(ics_timing PUBLIC ics::common)

ics_add_gtest(timing_test
  SOURCES
    test/clock_state_test.cpp
    test/ptp_client_test.cpp
    test/ptp_management_test.cpp
    test/publisher_test.cpp
    test/quality_test.cpp
    test/unix_socket_test.cpp
  LIBRARIES ics::timing)
target_include_directories(timing_test PRIVATE test)

# ptp4l's answers are parsed, so the decoder is fuzzed (ICS-008). The corpus
# holds responses captured from ptp4l 4.0.
ics_add_fuzzer(ptp_management_fuzz SOURCES fuzz/ptp_management_fuzz.cpp LIBRARIES ics::timing
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
