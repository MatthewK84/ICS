# Test targets (ICS-005). Every test links the allocation guard, so any test
# can open an ics::testing::NoAllocationScope; see
# testing/alloc_guard/include/ics/testing/no_allocation_scope.hpp.
#
#   ics_add_gtest(<name> SOURCES <file>... [LIBRARIES <target>...])
find_package(GTest CONFIG REQUIRED)

function(ics_add_gtest name)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBRARIES")
  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE ${ARG_LIBRARIES} ics_alloc_guard GTest::gtest_main)
  add_test(NAME ${name} COMMAND ${name})
endfunction()
