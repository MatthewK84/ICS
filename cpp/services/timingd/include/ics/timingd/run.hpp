#pragma once

#include <filesystem>
#include <span>
#include <string_view>

#include "ics/logging/logger.hpp"
#include "ics/timingd/config.hpp"

namespace ics::timingd {

// Exit statuses of ics-timingd (ICS-019).
inline constexpr int kExitStopped = 0;      // Stopped by SIGINT or SIGTERM.
inline constexpr int kExitUnavailable = 1;  // A socket could not be opened.
inline constexpr int kExitUsage = 2;        // A bad command line or config file.

// The config file ics-timingd reads. It is fixed rather than named on the
// command line, so the service only ever opens this one file.
inline constexpr std::string_view kConfigPath = "/etc/ics/ics-timingd.toml";

// Runs the service as configured, logging to logger, until SIGINT or SIGTERM:
// a step every poll interval. Blocks both signals in the calling thread, and
// leaves them blocked.
[[nodiscard]] int serve(const Config& config, const logging::Logger& logger);

// ics-timingd, which takes no arguments: reads the config file at path
// (kConfigPath, from main), then serves, logging to stderr. A bad config file
// is reported on stderr, naming each bad field.
[[nodiscard]] int run(std::span<const char* const> args, const std::filesystem::path& path);

}  // namespace ics::timingd
