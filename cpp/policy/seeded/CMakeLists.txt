# Seeded violations (ICS-005): one per Power-of-Ten rule, each written to
# break exactly that rule. Added only with -DICS_POLICY_SEEDS=ON, and never
# built by default. cpp/policy/check-policy.sh requires every seed to fail
# with the diagnostic named on its first line.

# Rejected by clang-tidy. Object libraries only so compile_commands.json lists
# them; the policy script never builds these.
foreach(seed IN ITEMS goto recursion function_size owning_memory malloc pointer_arithmetic mutable_global)
  add_library(seed_${seed} OBJECT EXCLUDE_FROM_ALL ${seed}.cpp)
endforeach()

# Rejected by the compiler warnings in cmake/Warnings.cmake.
foreach(seed IN ITEMS conversion nodiscard)
  add_library(seed_${seed} OBJECT EXCLUDE_FROM_ALL ${seed}.cpp)
endforeach()

# Rejected at run time by the allocation guard. Not registered with CTest.
add_executable(seed_post_init_allocation EXCLUDE_FROM_ALL post_init_allocation_test.cpp)
target_link_libraries(seed_post_init_allocation PRIVATE ics_alloc_guard GTest::gtest_main)
