#pragma once

#include <filesystem>
#include <span>
#include <string_view>

#include "ics/camd/config.hpp"
#include "ics/logging/logger.hpp"

namespace ics::camd {

// Exit statuses of ics-camd and ics-cam-offload (ICS-031).
inline constexpr int kExitStopped = 0;      // Stopped by SIGINT or SIGTERM; every camera verified.
inline constexpr int kExitUnavailable = 1;  // A socket could not be opened; a command or camera failed.
inline constexpr int kExitUsage = 2;        // A bad command line or config file.

// The config file ics-camd reads. It is fixed rather than named on the
// command line, so the service only ever opens this one file.
inline constexpr std::string_view kConfigPath = "/etc/ics/ics-camd.toml";

// Serves CameraService as configured, logging to logger, until SIGINT or
// SIGTERM. Blocks both signals in the calling thread, and leaves them
// blocked.
[[nodiscard]] int serve(const Config& config, const logging::Logger& logger);

// ics-camd, which takes no arguments: reads the config file at path
// (kConfigPath, from main), then serves, logging to stderr. A bad config file
// is reported on stderr, naming each bad field.
[[nodiscard]] int run(std::span<const char* const> args, const std::filesystem::path& path);

}  // namespace ics::camd
