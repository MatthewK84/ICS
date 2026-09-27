# Compiler warnings for ICS code (ICS-005): every warning is an error, with
# GCC 13 and Clang 17 alike. Conan dependencies are imported targets, whose
# headers CMake includes as system headers, so their warnings stay silent.
# cpp/policy/check-policy.sh proves these flags reject seeded violations.
add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Werror)
