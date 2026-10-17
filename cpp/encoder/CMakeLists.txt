# ics::encoder (ICS-036): each camera frame's line of sight. It takes the
# encoder time-tagger's datagrams (ICS-032), keeps the last seconds of axis
# angles, interpolates them to a frame's mid-exposure, and applies the
# station's pointing model, fitted by python/ics_models/pointing (ICS-035).
# See cpp/README.md.
find_package(OpenSSL CONFIG REQUIRED)

add_library(ics_encoder STATIC
  src/angles.cpp
  src/config.cpp
  src/frame_meta.cpp
  src/line_of_sight.cpp
  src/model_file.cpp
  src/monitor.cpp
  src/pointing_model.cpp
  src/receiver.cpp
  src/track.cpp)
add_library(ics::encoder ALIAS ics_encoder)
target_include_directories(ics_encoder PUBLIC include)
target_link_libraries(ics_encoder
  PUBLIC ics::common ics::config ics::logging ics::proto ics::tagger ics::timing
  PRIVATE OpenSSL::Crypto)

# The operational test of the encoder path, against the real tagger.
add_executable(ics-encoder-monitor tool/main.cpp)
target_link_libraries(ics-encoder-monitor PRIVATE ics::encoder)

ics_add_gtest(encoder_test
  SOURCES
    test/bench_test.cpp
    test/config_test.cpp
    test/frame_meta_test.cpp
    test/line_of_sight_test.cpp
    test/model_file_test.cpp
    test/monitor_test.cpp
    test/pointing_model_test.cpp
    test/receiver_test.cpp
    test/track_test.cpp
  LIBRARIES ics::encoder)
# The bench drives ICS-032's tagger core against its emulated station.
target_include_directories(encoder_test PRIVATE test ${PROJECT_SOURCE_DIR}/tagger/test)
target_compile_definitions(encoder_test PRIVATE
  ICS_GOLDEN_POINTING_DIR="${PROJECT_SOURCE_DIR}/../golden/pointing"
  ICS_ENCODER_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/ics-encoder.toml")

add_test(NAME ics-encoder-monitor_usage COMMAND ics-encoder-monitor --help)
set_tests_properties(ics-encoder-monitor_usage PROPERTIES PASS_REGULAR_EXPRESSION "usage: ics-encoder-monitor")

# The parameter file and the .meta files come from other tools, so both
# readers are fuzzed with any bytes.
ics_add_fuzzer(model_file_fuzz SOURCES fuzz/model_file_fuzz.cpp LIBRARIES ics::encoder
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/model_corpus)
ics_add_fuzzer(frame_meta_fuzz SOURCES fuzz/frame_meta_fuzz.cpp LIBRARIES ics::encoder
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/meta_corpus)
