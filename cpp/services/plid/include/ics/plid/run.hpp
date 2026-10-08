#pragma once

#include <filesystem>
#include <span>
#include <string_view>

#include "ics/logging/logger.hpp"
#include "ics/plid/config.hpp"

namespace ics::plid {

// Exit statuses of ics-plid (ICS-030).
inline constexpr int kExitStopped = 0;  // Stopped by SIGINT or SIGTERM.
inline constexpr int kExitFailed = 1;   // Could not start, or could not store.
inline constexpr int kExitUsage = 2;    // A bad command line or config file.

// The config file ics-plid reads. It is fixed rather than named on the
// command line, so the service only ever opens this one file.
inline constexpr std::string_view kConfigPath = "/etc/ics/ics-plid.toml";

// How long each wait for the feeds lasts at most: how late a tick, a sync or
// a stop signal may be noticed.
inline constexpr int kPollTimeoutMs = 100;

// Runs the service as configured, with the EGM96 grid at geoid, logging to
// logger, until SIGINT or SIGTERM or the first failure. Either way it closes
// the current segment and archives it before returning. Blocks both signals
// in the calling thread, and leaves them blocked.
[[nodiscard]] int serve(const Config& config, const std::filesystem::path& geoid, const logging::Logger& logger);

// ics-plid, which takes no arguments: reads the config file at path
// (kConfigPath, from main), then serves with the grid where the ICS images
// install it, logging to stderr. A bad config file is reported on stderr,
// naming each bad field.
[[nodiscard]] int run(std::span<const char* const> args, const std::filesystem::path& path);

}  // namespace ics::plid
