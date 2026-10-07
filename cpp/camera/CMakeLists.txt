# ics::camera (ICS-027): reads the cine files a Phantom saves and times their
# frames, drives a Phantom through a camera interface (an emulated camera
# until the Phantom SDK is available), and offloads and verifies segments.
add_library(ics_camera STATIC
  src/cine_read.cpp
  src/cine_write.cpp
  src/emulated_phantom.cpp
  src/frame_meta.cpp
  src/mapped_file.cpp
  src/offload.cpp)
add_library(ics::camera ALIAS ics_camera)
target_include_directories(ics_camera PUBLIC include)
target_link_libraries(ics_camera PUBLIC ics::common ics::proto)

ics_add_gtest(camera_test
  SOURCES
    test/cine_test.cpp
    test/emulated_phantom_test.cpp
    test/fixture_test.cpp
    test/frame_meta_test.cpp
    test/mapped_file_test.cpp
    test/offload_test.cpp
  LIBRARIES ics::camera)
target_include_directories(camera_test PRIVATE test)
target_compile_definitions(camera_test PRIVATE ICS_CAMERA_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")

# A cine comes off a camera link or a disk, so the reader is fuzzed with any
# bytes (ICS-008). The corpus holds cines the writer wrote.
ics_add_fuzzer(cine_fuzz SOURCES fuzz/cine_fuzz.cpp LIBRARIES ics::camera
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
