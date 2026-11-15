# ics::trigger (ICS-040, CPP-21): the trigger controller for one station. It
# predicts an engagement's closest approach, times a pulse from the station's
# PPS so that a Berkeley Nucleonics 577 fires the cameras then, and logs
# every step as an ics.v1.TriggerEvent. Held to the JSF AV rules
# (docs/cpp-jsf-av.md); python/ics_models/trigger is its reference. See
# cpp/README.md.
add_library(ics_trigger STATIC
  src/approach.cpp
  src/config.cpp
  src/controller.cpp
  src/events.cpp
  src/generator.cpp
  src/link.cpp
  src/scpi.cpp
  src/shot.cpp)
add_library(ics::trigger ALIAS ics_trigger)
target_include_directories(ics_trigger PUBLIC include)
target_link_libraries(ics_trigger PUBLIC ics::common ics::config ics::cue ics::frames ics::proto ics::store)
ics_jsf_target(ics_trigger)

# The tests read the golden files with the tracker's Golden_table.
ics_add_gtest(trigger_test
  SOURCES
    test/approach_test.cpp
    test/config_test.cpp
    test/controller_test.cpp
    test/events_test.cpp
    test/generator_test.cpp
    test/golden_test.cpp
    test/link_test.cpp
    test/replay_test.cpp
    test/scpi_test.cpp
    test/shot_test.cpp
  LIBRARIES ics::trigger)
ics_jsf_target(trigger_test)
target_include_directories(trigger_test PRIVATE test "${PROJECT_SOURCE_DIR}/tracker/test")
target_compile_definitions(trigger_test PRIVATE
  ICS_GOLDEN_TRIGGER_DIR="${PROJECT_SOURCE_DIR}/../golden/trigger"
  ICS_TRIGGER_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/ics-trigger.toml")

ics_add_fuzzer(trigger_reply_fuzz SOURCES fuzz/reply_fuzz.cpp LIBRARIES ics::trigger
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus/reply)
ics_add_fuzzer(trigger_events_fuzz SOURCES fuzz/events_fuzz.cpp LIBRARIES ics::trigger
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus/events)
