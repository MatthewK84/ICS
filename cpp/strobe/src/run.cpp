#include "ics/strobe/run.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>

#include "ics/common/error.hpp"
#include "ics/config/config.hpp"
#include "ics/config/reader.hpp"
#include "ics/logging/logger.hpp"
#include "ics/strobe/analyze.hpp"
#include "ics/strobe/config.hpp"

namespace ics::strobe {
namespace {

void log_offset(const logging::Logger& logger, const StrobeReport& report) {
  logger.info("camera_offset", {{"camera_id", report.offset.camera_id()},
                                {"offset_ns", report.offset.offset_ns()},
                                {"offset_sigma_ns", report.offset.offset_sigma_ns()},
                                {"measured_utc_ns", report.offset.measured_utc_ns()},
                                {"segments", static_cast<std::int64_t>(report.segments)},
                                {"lit", static_cast<std::int64_t>(report.lit)},
                                {"unlit", static_cast<std::int64_t>(report.unlit)},
                                {"scattered", static_cast<std::int64_t>(report.scattered)},
                                {"edge_frames", static_cast<std::int64_t>(report.fit.edge_frames)},
                                {"rms", report.fit.rms}});
}

}  // namespace

int analyze_and_publish(const AnalyzerConfig& config, const std::filesystem::path& folder,
                        const logging::Logger& logger) {
  const Result<StrobeReport> report = analyze_folder(folder, config.strobe);
  if (!report) {
    logger.error("analysis_failed", {{"error", to_string(report.error())}});
    return kExitFailed;
  }
  const Status published = publish(report->offset, config.station_id, config.offsets_file);
  if (!published) {
    logger.error("publish_failed", {{"error", to_string(published.error())}});
    return kExitFailed;
  }
  log_offset(logger, *report);
  return kExitPublished;
}

int run(const std::span<const char* const> args, const std::filesystem::path& config_path,
        const std::filesystem::path& folder) {
  if (args.size() != 1) {
    std::fprintf(stderr, "usage: ics-strobe-analyzer\nIt analyzes the cines in the working folder; the config file is %s.\n",
                 config_path.c_str());
    return kExitUsage;
  }
  const auto config = config::read_file(config_path, &read_analyzer_config);
  if (!config) {
    std::fputs(config::format_errors(config_path.native(), config.error()).c_str(), stderr);
    return kExitUsage;
  }
  if (!config->strobe.schedule.valid()) {
    std::fprintf(stderr, "%s: the strobe's pulses must end within their second\n", config_path.c_str());
    return kExitUsage;
  }
  const logging::Logger logger = logging::Logger::to_stderr(config->log.service, config->log.level);
  return analyze_and_publish(*config, folder, logger);
}

}  // namespace ics::strobe
