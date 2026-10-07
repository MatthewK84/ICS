# ics::camera (ICS-027, ICS-028): reads the cine files a Phantom saves and
# times their frames, drives a Phantom and a FLIR X6980-HS through one camera
# interface (emulated cameras until their SDKs are available), and offloads
# and verifies segments.
add_library(ics_camera STATIC
  src/cine_read.cpp
  src/cine_write.cpp
  src/emulated_phantom.cpp
  src/emulated_x6980.cpp
  src/emulation.cpp
  src/file_out.cpp
  src/flir_camera.cpp
  src/frame_meta.cpp
  src/image.cpp
  src/irig.cpp
  src/mapped_file.cpp
  src/offload.cpp
  src/strobe.cpp)
add_library(ics::camera ALIAS ics_camera)
target_include_directories(ics_camera PUBLIC include)
target_link_libraries(ics_camera PUBLIC ics::common ics::proto)

ics_add_gtest(camera_test
  SOURCES
    test/cine_test.cpp
    test/emulated_phantom_test.cpp
    test/emulated_x6980_test.cpp
    test/fixture_test.cpp
    test/flir_camera_test.cpp
    test/frame_meta_test.cpp
    test/image_test.cpp
    test/irig_test.cpp
    test/mapped_file_test.cpp
    test/offload_test.cpp
    test/strobe_scene_test.cpp
  LIBRARIES ics::camera)
target_include_directories(camera_test PRIVATE test)
target_compile_definitions(camera_test PRIVATE ICS_CAMERA_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")

# A cine comes off a camera link or a disk, so the reader is fuzzed with any
# bytes (ICS-008). The corpus holds cines the writer wrote.
ics_add_fuzzer(cine_fuzz SOURCES fuzz/cine_fuzz.cpp LIBRARIES ics::camera
  CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
