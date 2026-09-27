# toolchain_check: a small library, program, test and fuzz target that prove the toolchain
# builds reproducibly with each compiler (ICS-004). It stays as a canary.
add_library(toolchain_check STATIC src/checksum.cpp)
target_include_directories(toolchain_check PUBLIC include)

add_executable(toolchain_check_cli src/main.cpp)
target_link_libraries(toolchain_check_cli PRIVATE toolchain_check)

ics_add_gtest(toolchain_check_test SOURCES test/checksum_test.cpp LIBRARIES toolchain_check)
ics_add_fuzzer(fnv1a32_fuzz SOURCES fuzz/fnv1a32_fuzz.cpp LIBRARIES toolchain_check CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus)
