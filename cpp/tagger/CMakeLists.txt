# ics::tagger (ICS-032): the encoder time-tagger's core, the part that runs the
# same on any board. It keeps UTC on the tagger's counter from the GNSS PPS
# edge, stamps the mount encoders' samples and packs them into datagrams, and
# reads the datagrams back for the host. It allocates nothing after start-up
# and throws nothing, so it builds for the tagger's MCU as well; the board
# port is in firmware/encoder_tagger. See cpp/README.md.
add_library(ics_tagger STATIC
  src/datagram.cpp
  src/pps_clock.cpp
  src/sampler.cpp)
add_library(ics::tagger ALIAS ics_tagger)
target_include_directories(ics_tagger PUBLIC include)
target_link_libraries(ics_tagger PUBLIC ics::common)

ics_add_gtest(tagger_test
  SOURCES
    test/bench_test.cpp
    test/counter_test.cpp
    test/datagram_test.cpp
    test/pps_clock_test.cpp
    test/sampler_test.cpp
  LIBRARIES ics::tagger)
target_include_directories(tagger_test PRIVATE test)

# A datagram comes off the station network, so the decoder is fuzzed with any
# bytes.
ics_add_fuzzer(datagram_fuzz SOURCES fuzz/datagram_fuzz.cpp LIBRARIES ics::tagger
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
