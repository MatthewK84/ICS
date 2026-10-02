# ics-timingd (ICS-019): reads the station's PTP state from ptp4l and publishes
# ics.v1.TimeQuality reports; see cpp/README.md.
add_library(ics_timingd STATIC src/config.cpp src/report.cpp src/run.cpp src/service.cpp)
add_library(ics::timingd ALIAS ics_timingd)
target_include_directories(ics_timingd PUBLIC include)
target_link_libraries(ics_timingd PUBLIC ics::config ics::logging ics::proto ics::timing)

add_executable(ics-timingd src/main.cpp)
target_link_libraries(ics-timingd PRIVATE ics::timingd)

ics_add_gtest(timingd_test
  SOURCES
    test/config_test.cpp
    test/report_test.cpp
    test/run_test.cpp
    test/service_test.cpp
  LIBRARIES ics::timingd)
target_include_directories(timingd_test PRIVATE ${PROJECT_SOURCE_DIR}/timing/test test)
target_compile_definitions(timingd_test PRIVATE ICS_TIMINGD_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/ics-timingd.toml")

# The executable itself, given an argument it does not take: it prints its
# usage. This also covers main.cpp.
add_test(NAME ics-timingd_usage COMMAND ics-timingd --help)
set_tests_properties(ics-timingd_usage PROPERTIES PASS_REGULAR_EXPRESSION
                     "^usage: ics-timingd\nThe config file is /etc/ics/ics-timingd.toml.\n$")
