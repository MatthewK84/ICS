// main.cpp: ics-cue-replay (ICS-037), the operational test of the cue
// extrapolator. It replays the .icspli segment files in the working folder
// through the [cue] table of /etc/ics/ics-cue.toml and writes the station's cue
// every 20 ms as a JSON line. Neither path is an argument: CodeQL's
// path-injection rule rejects a path taken from the command line. See
// ics/cue/replay.h and cpp/README.md.
//
// Usage: ics-cue-replay
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>
#include <system_error>

#include <ics/cue/replay.h>

// JSF deviation D-7: the C++ standard fixes main's signature (AV 97 and 209).
int main(const int argc, char* argv[])
{
  // A working folder that cannot be found is no path, which holds no segments.
  std::error_code error;
  return ics::cue::run_replay(std::span<const char* const>(argv, static_cast<std::size_t>(argc)),
                              "/etc/ics/ics-cue.toml",
                              std::filesystem::current_path(error),
                              std::cout,
                              std::cerr);
}
