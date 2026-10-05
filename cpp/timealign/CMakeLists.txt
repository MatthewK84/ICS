# ics::timealign (ICS-026): fits each vehicle sortie's boot clock to UTC as an
# offset and a drift, times records and events by it, summarizes link latency,
# and checks the fit against an injected drift.
add_library(ics_timealign STATIC
  src/align.cpp
  src/check.cpp
  src/clock_fit.cpp
  src/latency.cpp
  src/sortie.cpp)
add_library(ics::timealign ALIAS ics_timealign)
target_include_directories(ics_timealign PUBLIC include)
target_link_libraries(ics_timealign PUBLIC ics::common ics::proto)

ics_add_gtest(timealign_test
  SOURCES
    test/align_test.cpp
    test/check_test.cpp
    test/clock_fit_test.cpp
    test/fixture_test.cpp
    test/latency_test.cpp
    test/sortie_test.cpp
  LIBRARIES ics::timealign)
target_compile_definitions(timealign_test PRIVATE
  ICS_TIMEALIGN_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")

# The fit takes pairs from logs and links ICS does not control, so it is
# fuzzed for overflow and for its promises (ICS-008).
ics_add_fuzzer(clock_fit_fuzz SOURCES fuzz/clock_fit_fuzz.cpp LIBRARIES ics::timealign
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
