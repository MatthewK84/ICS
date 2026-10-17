// ics-encoder-monitor (ICS-036): the operational test of the encoder path.
// It listens for the encoder time-tagger's datagrams for SECONDS, logging
// what it hears once a second, then logs the line of sight of each frame in
// the camera .meta files in the working folder. Neither path is an argument:
// CodeQL's path-injection rule rejects a path taken from the command line.
// See ics/encoder/config.hpp for the config file, and cpp/README.md.
//
// Usage: ics-encoder-monitor SECONDS

#include <cstddef>
#include <filesystem>
#include <span>
#include <system_error>

#include "ics/encoder/monitor.hpp"

int main(const int argc, char* argv[]) {
  // A working folder that cannot be found is no path, which holds no .meta
  // files.
  std::error_code error;
  return ics::encoder::run(std::span<const char* const>(argv, static_cast<std::size_t>(argc)),
                           "/etc/ics/ics-encoder.toml", std::filesystem::current_path(error));
}
