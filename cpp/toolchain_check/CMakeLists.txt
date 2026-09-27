# toolchain_check: a small library, program and test that prove the toolchain
# builds reproducibly with each compiler (ICS-004). It stays as a canary.
add_library(toolchain_check STATIC src/checksum.cpp)
target_include_directories(toolchain_check PUBLIC include)

add_executable(toolchain_check_cli src/main.cpp)
target_link_libraries(toolchain_check_cli PRIVATE toolchain_check)

find_package(GTest CONFIG REQUIRED)
add_executable(toolchain_check_test test/checksum_test.cpp)
target_link_libraries(toolchain_check_test PRIVATE toolchain_check GTest::gtest_main)
add_test(NAME toolchain_check_test COMMAND toolchain_check_test)
