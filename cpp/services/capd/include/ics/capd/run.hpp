#pragma once

#include <filesystem>
#include <span>
#include <string_view>

#include "ics/capd/config.hpp"
#include "ics/logging/logger.hpp"

namespace ics::capd {

// Exit statuses of ics-capd (ICS-020).
inline constexpr int kExitStopped = 0;  // Stopped by SIGINT or SIGTERM.
inline constexpr int kExitFailed = 1;   // A port or file failed, at start or while capturing.
inline constexpr int kExitUsage = 2;    // A bad command line or config file.

// The config file ics-capd reads. It is fixed rather than named on the
// command line, so the service only ever opens this one file.
inline constexpr std::string_view kConfigPath = "/etc/ics/ics-capd.toml";

// Captures as configured, logging to logger, until SIGINT or SIGTERM, then
// closes every file. Blocks both signals in the calling thread, and leaves
// them blocked. A failure while capturing also closes the files, then
// returns kExitFailed.
[[nodiscard]] int serve(const Config& config, const logging::Logger& logger);

// ics-capd, which takes no arguments: reads the config file at path
// (kConfigPath, from main), then serves, logging to stderr. A bad config file
// is reported on stderr, naming each bad field.
[[nodiscard]] int run(std::span<const char* const> args, const std::filesystem::path& path);

}  // namespace ics::capd
