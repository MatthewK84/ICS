# ics::config (ICS-016): TOML config files, in the subset include/ics/config/subset.hpp
# defines, checked against schemas written as code. Invalid config fails at
# start-up, naming every bad field.
find_package(tl-expected CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

add_library(ics_config STATIC src/config.cpp src/logging_config.cpp src/reader.cpp src/subset.cpp)
add_library(ics::config ALIAS ics_config)
target_include_directories(ics_config PUBLIC include)
target_link_libraries(ics_config PUBLIC ics::common ics::logging tl::expected tomlplusplus::tomlplusplus)

ics_add_gtest(config_test
  SOURCES
    test/config_test.cpp
    test/logging_config_test.cpp
    test/reader_test.cpp
    test/subset_test.cpp
  LIBRARIES ics::config)

# The loader parses files an operator writes, so it is fuzzed (ICS-008).
ics_add_fuzzer(config_fuzz SOURCES fuzz/config_fuzz.cpp LIBRARIES ics::config CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
