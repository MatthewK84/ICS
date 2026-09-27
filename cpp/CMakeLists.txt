# ICS C++ build (ICS-004). Configure through cpp/CMakePresets.json after
# installing dependencies with Conan; see cpp/README.md.
cmake_minimum_required(VERSION 3.25)
project(ics VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/Reproducible.cmake)

enable_testing()
add_subdirectory(toolchain_check)
