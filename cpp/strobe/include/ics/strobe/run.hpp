#pragma once

#include <filesystem>
#include <span>

#include "ics/logging/logger.hpp"
#include "ics/strobe/config.hpp"

namespace ics::strobe {

// ics-strobe-analyzer's exit codes.
inline constexpr int kExitPublished = 0;  // The offset was measured and published.
inline constexpr int kExitFailed = 1;     // The cines could not be analyzed, or the offset published.
inline constexpr int kExitUsage = 2;      // A bad command line or config file.

// Analyzes the cines in folder with config, publishes the offset to its
// offsets file, and logs "camera_offset" with the offset, its sigma and the
// segments behind it; or logs "analysis_failed" or "publish_failed" with the
// error.
[[nodiscard]] int analyze_and_publish(const AnalyzerConfig& config, const std::filesystem::path& folder,
                                      const logging::Logger& logger);

// The command: no arguments, the config file at config_path, the cines in
// folder.
[[nodiscard]] int run(std::span<const char* const> args, const std::filesystem::path& config_path,
                      const std::filesystem::path& folder);

}  // namespace ics::strobe
