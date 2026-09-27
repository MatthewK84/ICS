# Deterministic builds (ICS-004): the same sources, toolchain and dependencies
# must give byte-identical outputs, whatever directory the build runs in.
# deploy/toolchain/check-reproducible.sh verifies this in CI.

# Keep source and build paths out of objects, debug info and macros.
add_compile_options(
  "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=src"
  "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build"
  -Werror=date-time
)

# Deterministic static archives: zero timestamps, owners and modes.
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> Dqc <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_APPEND "<CMAKE_AR> Dq <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> -D <TARGET>")
