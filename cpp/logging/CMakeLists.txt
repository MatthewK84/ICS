# ics::logging (ICS-016): structured JSON log lines written through spdlog.
# Not for real-time paths; see include/ics/logging/logger.hpp.
find_package(spdlog CONFIG REQUIRED)

add_library(ics_logging STATIC src/json_line.cpp src/logger.cpp)
add_library(ics::logging ALIAS ics_logging)
target_include_directories(ics_logging PUBLIC include)
target_link_libraries(ics_logging PUBLIC ics::common PRIVATE spdlog::spdlog_header_only)

ics_add_gtest(logging_test
  SOURCES
    test/json_line_test.cpp
    test/logger_test.cpp
  LIBRARIES ics::logging)
