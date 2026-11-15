# ics::handoff (ICS-039, CPP-20): the hand-off scheduler for one station. It
# keeps a run plan's engagements in the order they will happen, gives the
# tracker the cue of the one the station serves, and slews the mount to the
# next. Held to the JSF AV rules (docs/cpp-jsf-av.md);
# python/ics_models/handoff is its reference. See cpp/README.md.
add_library(ics_handoff STATIC
  src/approach.cpp
  src/config.cpp
  src/queue.cpp
  src/scheduler.cpp
  src/slew.cpp)
add_library(ics::handoff ALIAS ics_handoff)
target_include_directories(ics_handoff PUBLIC include)
target_link_libraries(ics_handoff
  PUBLIC ics::common ics::config ics::cue ics::encoder ics::frames ics::mount ics::proto ics::tracker)
ics_jsf_target(ics_handoff)

# The tests paint their frames with the tracker's Frame_maker and read the
# golden files with its Golden_table.
ics_add_gtest(handoff_test
  SOURCES
    test/approach_test.cpp
    test/config_test.cpp
    test/golden_test.cpp
    test/queue_test.cpp
    test/replay_test.cpp
    test/scheduler_test.cpp
    test/slew_test.cpp
  LIBRARIES ics::handoff)
ics_jsf_target(handoff_test)
target_include_directories(handoff_test PRIVATE test "${PROJECT_SOURCE_DIR}/tracker/test")
target_compile_definitions(handoff_test PRIVATE
  ICS_GOLDEN_HANDOFF_DIR="${PROJECT_SOURCE_DIR}/../golden/handoff"
  ICS_HANDOFF_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/ics-handoff.toml")
