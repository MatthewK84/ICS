# Allocation guard for tests (ICS-005). An object library, so the replacement
# operator new and delete are always linked into the test, whether or not it
# uses NoAllocationScope.
add_library(ics_alloc_guard OBJECT src/allocation_hook.cpp)
target_include_directories(ics_alloc_guard PUBLIC include)
target_link_libraries(ics_alloc_guard PUBLIC GTest::gtest)

ics_add_gtest(no_allocation_scope_test SOURCES test/no_allocation_scope_test.cpp)
# The test calls the sized operator delete forms directly. GCC declares them by
# default; Clang 17 needs this flag (Clang 19 turns it on by default).
target_compile_options(no_allocation_scope_test PRIVATE $<$<CXX_COMPILER_ID:Clang>:-fsized-deallocation>)
# The test asks for impossible sizes; sanitizers must return null, not abort.
set_tests_properties(no_allocation_scope_test PROPERTIES
  ENVIRONMENT "ASAN_OPTIONS=allocator_may_return_null=1;TSAN_OPTIONS=allocator_may_return_null=1")
