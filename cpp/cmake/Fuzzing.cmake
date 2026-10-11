# Fuzz targets (ICS-008). Every build compiles each fuzz source, so the
# warnings and clang-tidy cover it. With -DICS_FUZZ=ON (the clang-fuzz preset)
# it also links a libFuzzer executable and lists it in <build>/fuzzers.txt,
# which cpp/policy/check-dynamic.sh reads. CI no longer fuzzes (#159); run
# "cpp/policy/check-dynamic.sh fuzz SECONDS" by hand.
#
#   ics_add_fuzzer(<name> SOURCES <file>... CORPUS <folder> [LIBRARIES <target>...])
option(ICS_FUZZ "Link the libFuzzer targets; needs Clang and the clang-fuzz preset" OFF)

function(ics_add_fuzzer name)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "CORPUS" "SOURCES;LIBRARIES")
  if(NOT ICS_FUZZ)
    add_library(${name} OBJECT ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_LIBRARIES})
    return()
  endif()
  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE ${ARG_LIBRARIES})
  target_link_options(${name} PRIVATE -fsanitize=fuzzer)
  set_property(GLOBAL APPEND PROPERTY ICS_FUZZERS "$<TARGET_FILE:${name}> ${ARG_CORPUS}")
endfunction()

# Write <build>/fuzzers.txt: one "executable seed-corpus" line per fuzz target.
function(ics_write_fuzzer_list)
  get_property(fuzzers GLOBAL PROPERTY ICS_FUZZERS)
  list(JOIN fuzzers "\n" lines)
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/fuzzers.txt" CONTENT "${lines}\n")
endfunction()
