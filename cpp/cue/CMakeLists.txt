# ics::cue (ICS-037): the cue extrapolator for one target. It fuses the
# target's PLI and radar reports in a constant-acceleration Kalman filter in
# the range ENU frame, and gives a station's azimuth, elevation and their
# rates at the moment a mount command takes effect. The first module held to
# the JSF AV rules (docs/cpp-jsf-av.md); python/ics_models/cue is its
# reference. See cpp/README.md.
add_library(ics_cue STATIC
  src/config.cpp
  src/extrapolator.cpp
  src/filter.cpp
  src/kinematics.cpp
  src/measurement.cpp
  src/pointing.cpp
  src/replay.cpp)
add_library(ics::cue ALIAS ics_cue)
target_include_directories(ics_cue PUBLIC include)
target_link_libraries(ics_cue PUBLIC ics::common ics::config ics::frames ics::proto ics::store)
ics_jsf_target(ics_cue)

# The operational test of the cue, against ics-plid's segment files.
add_executable(ics-cue-replay tool/main.cpp)
target_link_libraries(ics-cue-replay PRIVATE ics::cue)
ics_jsf_target(ics-cue-replay)

ics_add_gtest(cue_test
  SOURCES
    test/config_test.cpp
    test/extrapolator_test.cpp
    test/filter_test.cpp
    test/golden_test.cpp
    test/kinematics_test.cpp
    test/measurement_test.cpp
    test/pointing_test.cpp
    test/replay_test.cpp
    test/replay_tool_test.cpp
  LIBRARIES ics::cue)
ics_jsf_target(cue_test)
target_include_directories(cue_test PRIVATE test)
target_compile_definitions(cue_test PRIVATE
  ICS_GOLDEN_CUE_DIR="${PROJECT_SOURCE_DIR}/../golden/cue"
  ICS_CUE_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/ics-cue.toml")

add_test(NAME ics-cue-replay_usage COMMAND ics-cue-replay --help)
set_tests_properties(ics-cue-replay_usage PROPERTIES PASS_REGULAR_EXPRESSION "usage: ics-cue-replay")
