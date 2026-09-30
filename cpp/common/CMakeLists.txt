# ics::common (ICS-015): the ICS error type, the ics::check assertion and the
# fixed-capacity containers that real-time code builds on.
find_package(tl-expected CONFIG REQUIRED)

add_library(ics_common STATIC src/check.cpp src/error.cpp)
add_library(ics::common ALIAS ics_common)
target_include_directories(ics_common PUBLIC include)
target_link_libraries(ics_common PUBLIC tl::expected)

ics_add_gtest(common_test
  SOURCES
    test/check_test.cpp
    test/error_test.cpp
    test/fixed_pool_test.cpp
    test/ring_buffer_test.cpp
    test/static_vector_test.cpp
  LIBRARIES ics::common)

# Real-time targets will build without exceptions, so the headers must too.
# This object library is compiled with -fno-exceptions and never linked.
add_library(ics_common_no_exceptions OBJECT test/no_exceptions_check.cpp)
target_link_libraries(ics_common_no_exceptions PRIVATE ics::common)
target_compile_options(ics_common_no_exceptions PRIVATE -fno-exceptions)
