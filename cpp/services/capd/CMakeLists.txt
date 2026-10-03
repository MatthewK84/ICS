# ics-capd (ICS-020): captures the station's TAP ports to rotating, hashed
# pcap files; see cpp/README.md.
add_library(ics_capd STATIC src/config.cpp src/run.cpp src/service.cpp)
add_library(ics::capd ALIAS ics_capd)
target_include_directories(ics_capd PUBLIC include)
target_link_libraries(ics_capd PUBLIC ics::capture ics::config ics::logging ics::timing)

add_executable(ics-capd src/main.cpp)
target_link_libraries(ics-capd PRIVATE ics::capd)

ics_add_gtest(capd_test
  SOURCES
    test/config_test.cpp
    test/run_test.cpp
    test/service_test.cpp
  LIBRARIES ics::capd)
target_include_directories(capd_test PRIVATE ${PROJECT_SOURCE_DIR}/timing/test ${PROJECT_SOURCE_DIR}/capture/test test)
target_compile_definitions(capd_test PRIVATE ICS_CAPD_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/ics-capd.toml")

# The executable itself, given an argument it does not take: it prints its
# usage. This also covers main.cpp.
add_test(NAME ics-capd_usage COMMAND ics-capd --help)
set_tests_properties(ics-capd_usage PROPERTIES PASS_REGULAR_EXPRESSION
                     "^usage: ics-capd\nThe config file is /etc/ics/ics-capd.toml.\n$")
