// ics-strobe-analyzer (ICS-029): measures a camera's time offset from the
// PPS strobe segments it recorded, the cine files in the working folder, and
// publishes it to the station's camera offsets file for ics-timingd. Neither
// path is an argument: CodeQL's path-injection rule rejects a path taken from
// the command line. See ics/strobe/config.hpp for the config file.
//
// Usage: ics-strobe-analyzer

#include <cstddef>
#include <filesystem>
#include <span>
#include <system_error>

#include "ics/strobe/run.hpp"

int main(const int argc, char* argv[]) {
  // A working folder that cannot be found is no path, which fails as a
  // folder that cannot be listed.
  std::error_code error;
  return ics::strobe::run(std::span<const char* const>(argv, static_cast<std::size_t>(argc)),
                          "/etc/ics/ics-strobe.toml", std::filesystem::current_path(error));
}
