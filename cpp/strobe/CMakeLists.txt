# ics::strobe (ICS-029): measures each camera's time offset from the PPS
# strobe frames it recorded, and publishes it to the station's camera offsets
# file, which ics-timingd copies into its TimeQuality reports.
add_library(ics_strobe STATIC src/analyze.cpp src/config.cpp src/detect.cpp src/fit.cpp src/run.cpp)
add_library(ics::strobe ALIAS ics_strobe)
target_include_directories(ics_strobe PUBLIC include)
target_link_libraries(ics_strobe PUBLIC ics::camera ics::config ics::logging ics::proto ics::timing)

add_executable(ics-strobe-analyzer tool/main.cpp)
target_link_libraries(ics-strobe-analyzer PRIVATE ics::strobe)

ics_add_gtest(strobe_test
  SOURCES
    test/analyze_test.cpp
    test/config_test.cpp
    test/detect_test.cpp
    test/fit_test.cpp
    test/run_test.cpp
  LIBRARIES ics::strobe)
target_include_directories(strobe_test PRIVATE test ${PROJECT_SOURCE_DIR}/camera/test)
target_compile_definitions(strobe_test PRIVATE ICS_STROBE_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/ics-strobe.toml")

add_test(NAME ics-strobe-analyzer_usage COMMAND ics-strobe-analyzer --help)
set_tests_properties(ics-strobe-analyzer_usage PROPERTIES PASS_REGULAR_EXPRESSION "usage: ics-strobe-analyzer")
