# Seeded runtime defects (ICS-008): one per dynamic check, each written to
# trip exactly that check. Added only with -DICS_POLICY_SEEDS=ON and never
# built by default. cpp/policy/check-dynamic.sh requires every seed to fail
# with the report named on its first line.

# Caught by a sanitizer at run time.
foreach(seed IN ITEMS asan_heap_overflow ubsan_signed_overflow tsan_data_race)
  add_executable(seed_${seed} EXCLUDE_FROM_ALL ${seed}.cpp)
endforeach()

# Caught by libFuzzer within its time budget; linked only in the clang-fuzz build.
if(ICS_FUZZ)
  add_executable(seed_fuzz_planted_bug EXCLUDE_FROM_ALL fuzz_planted_bug.cpp)
  target_link_options(seed_fuzz_planted_bug PRIVATE -fsanitize=fuzzer)
endif()
