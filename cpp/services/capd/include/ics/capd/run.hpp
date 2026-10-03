#pragma once

#include <filesystem>
#include <span>
#include <string_view>

#include "ics/capd/config.hpp"
#include "ics/logging/logger.hpp"

namespace ics::capd {

// Exit statuses of ics-capd (ICS-020).
inline constexpr int kExitStopped = 0;  // Stopped by SIGINT or SIGTERM.
inline constexpr int kExitFailed = 1;   // Could not start, or a capture or write failed.
inline constexpr int kExitUsage = 2;    // A bad command line or config file.

// The config file ics-capd reads. It is fixed rather than named on the
// command line, so the service only ever opens this one file.
inline constexpr std::string_view kConfigPath = "/etc/ics/ics-capd.toml";

// How long each wait for packets lasts at most: how late a file may rotate,
// and a stop signal be noticed.
inline constexpr int kPollTimeoutMs = 100;

// Runs the service as configured, logging to logger, until SIGINT or SIGTERM
// or the first failure. Either way it closes and hashes every open file
// before returning. Blocks both signals in the calling thread, and leaves
// them blocked.
[[nodiscard]] int serve(const Config& config, const logging::Logger& logger);

// ics-capd, which takes no arguments: reads the config file at path
// (kConfigPath, from main), then serves, logging to stderr. A bad config file
// is reported on stderr, naming each bad field.
[[nodiscard]] int run(std::span<const char* const> args, const std::filesystem::path& path);

}  // namespace ics::capd
