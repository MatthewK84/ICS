# JSF AV C++ (ICS-037): the warnings the C++ under cpp/policy/jsf-paths.txt
# compiles with, on top of cmake/Warnings.cmake's -Wall -Wextra -Wpedantic
# -Wconversion -Werror. Each one checks a JSF AV rule; see
# docs/cpp-jsf-av.md. The Clang build (the CI policy check) and the GCC
# build each add what only their compiler has.
set(ICS_JSF_WARNINGS
  -Wshadow                 # AV 135: no name hides one in an outer scope
  -Wold-style-cast         # AV 185: C++ casts only
  -Wfloat-equal            # AV 202: no exact floating-point equality
  -Wsign-conversion        # AV 162: signed and unsigned are not mixed
  -Wcast-qual              # AV 183: no cast that drops const
  -Wnon-virtual-dtor       # AV 78: a base with virtual functions has a virtual destructor
  -Wimplicit-fallthrough   # AV 193: every non-empty case ends
  -Wvla                    # AV 206: no stack arrays sized at run time
  $<$<CXX_COMPILER_ID:Clang>:-Wcomma>                       # AV 168: no comma operator
  $<$<CXX_COMPILER_ID:Clang>:-Wunreachable-code-aggressive> # AV 186: no unreachable code
  $<$<CXX_COMPILER_ID:Clang>:-Wmissing-braces>              # AV 144: braces match the structure
  $<$<CXX_COMPILER_ID:GNU>:-Wuseless-cast>)                 # AV 181: no redundant casts

# Compiles a whole target, such as a JSF module's library, with the JSF
# warnings.
function(ics_jsf_target target)
  target_compile_options(${target} PRIVATE ${ICS_JSF_WARNINGS})
endfunction()

# Compiles the given sources with the JSF warnings, for a JSF file in a target
# that is not, such as a test in a module's shared test binary.
function(ics_jsf_sources)
  set_property(SOURCE ${ARGN} APPEND PROPERTY COMPILE_OPTIONS ${ICS_JSF_WARNINGS})
endfunction()
