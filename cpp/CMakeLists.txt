# ICS C++ build (ICS-004, ICS-005). Configure through cpp/CMakePresets.json after
# installing dependencies with Conan; see cpp/README.md.
cmake_minimum_required(VERSION 3.25)
project(ics VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ICS_POLICY_SEEDS "Add the seeded Power-of-Ten violations; for cpp/policy/check-policy.sh only" OFF)

include(cmake/Reproducible.cmake)
include(cmake/Warnings.cmake)

enable_testing()
include(cmake/Testing.cmake)
add_subdirectory(testing/alloc_guard)
add_subdirectory(toolchain_check)

if(ICS_POLICY_SEEDS)
  add_subdirectory(policy/seeded)
endif()
