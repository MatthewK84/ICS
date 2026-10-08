# ICS C++ build (ICS-004, ICS-005, ICS-008). Configure through cpp/CMakePresets.json after
# installing dependencies with Conan; see cpp/README.md.
cmake_minimum_required(VERSION 3.25)
project(ics VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ICS_POLICY_SEEDS "Add the seeded Power-of-Ten violations; for cpp/policy/check-policy.sh only" OFF)

include(cmake/Reproducible.cmake)
# Generated protobuf code (ICS-011) is added before the ICS warning flags, like
# a dependency; see proto/CMakeLists.txt.
add_subdirectory(proto)
include(cmake/Warnings.cmake)

enable_testing()
include(cmake/Testing.cmake)
include(cmake/Fuzzing.cmake)
add_subdirectory(testing/alloc_guard)
add_subdirectory(common)
add_subdirectory(logging)
add_subdirectory(config)
add_subdirectory(frames)
add_subdirectory(timing)
add_subdirectory(capture)
add_subdirectory(mavlink)
add_subdirectory(cot)
add_subdirectory(lattice)
add_subdirectory(sapient)
add_subdirectory(flightlog)
add_subdirectory(timealign)
add_subdirectory(camera)
add_subdirectory(strobe)
add_subdirectory(store)
add_subdirectory(services/timingd)
add_subdirectory(services/capd)
add_subdirectory(testing/capture_bench)
add_subdirectory(testing/mavlink_replay)
add_subdirectory(testing/log_import)
add_subdirectory(testing/time_align)
add_subdirectory(testing/camera_bench)
add_subdirectory(testing/strobe_bench)
add_subdirectory(testing/lattice_probe)
add_subdirectory(toolchain_check)
add_subdirectory(proto/test)

if(ICS_POLICY_SEEDS)
  add_subdirectory(policy/seeded)
  add_subdirectory(policy/seeded-runtime)
endif()

ics_write_fuzzer_list()
