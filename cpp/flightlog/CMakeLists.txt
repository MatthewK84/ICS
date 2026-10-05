# ics::flightlog (ICS-025): imports PX4 ULog and ArduPilot DataFlash onboard
# logs, and turns their estimator states, raw GNSS fixes and events into
# ics.v1.PliRecord and PliEvent with UTC times from the logs' GNSS time.
add_library(ics_flightlog STATIC
  src/dataflash.cpp
  src/gps_time.cpp
  src/ulog.cpp)
add_library(ics::flightlog ALIAS ics_flightlog)
target_include_directories(ics_flightlog PUBLIC include)
target_link_libraries(ics_flightlog PUBLIC ics::common ics::frames ics::proto)

ics_add_gtest(flightlog_test
  SOURCES
    test/dataflash_samples_test.cpp
    test/dataflash_test.cpp
    test/gps_time_test.cpp
    test/ulog_samples_test.cpp
    test/ulog_test.cpp
  LIBRARIES ics::flightlog)
target_include_directories(flightlog_test PRIVATE test)
target_compile_definitions(flightlog_test PRIVATE
  ICS_EGM96_PATH="${ICS_GEOID_DIR}/egm96-5.pgm"
  ICS_FLIGHTLOG_LOGS="${CMAKE_CURRENT_SOURCE_DIR}/test/logs")
