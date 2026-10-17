#include "ics/encoder/monitor.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/config/config.hpp"
#include "ics/config/reader.hpp"
#include "ics/encoder/angles.hpp"
#include "ics/encoder/config.hpp"
#include "ics/encoder/frame_meta.hpp"
#include "ics/encoder/line_of_sight.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "ics/encoder/receiver.hpp"
#include "ics/encoder/track.hpp"
#include "ics/logging/logger.hpp"
#include "ics/tagger/sampler.hpp"
#include "ics/v1/camera_frame_meta.pb.h"

namespace ics::encoder {
namespace {

using std::chrono::steady_clock;

constexpr std::chrono::seconds kReportEvery{1};
constexpr std::int64_t kSamplesPerSecond = 1'000;

[[nodiscard]] std::int64_t count(const std::uint64_t value) noexcept { return static_cast<std::int64_t>(value); }

// SECONDS from the command line: 1 to kMaxListenSeconds.
[[nodiscard]] std::optional<std::chrono::seconds> listen_seconds(const std::string_view text) noexcept {
  std::int64_t seconds = 0;
  const std::from_chars_result read = std::from_chars(text.begin(), text.end(), seconds);
  if (read.ec != std::errc() || read.ptr != text.end() || seconds < 1 || seconds > kMaxListenSeconds) {
    return std::nullopt;
  }
  return std::chrono::seconds(seconds);
}

// Room for every sample heard while listening, and 10 s more for a clock
// that runs fast.
[[nodiscard]] std::size_t history_for(const std::chrono::seconds length) noexcept {
  return static_cast<std::size_t>(length.count() * kSamplesPerSecond) + kDefaultHistory;
}

// Listens and reports, once the config is read.
[[nodiscard]] int monitor(const MonitorConfig& config, const std::chrono::seconds length,
                          const std::filesystem::path& folder, const logging::Logger& logger) {
  const EncoderSettings& settings = config.encoder;
  const Result<LoadedModel> loaded = load_pointing_model(settings.pointing_model_file);
  if (!loaded) {
    logger.error("model_unusable",
                 {{"file", settings.pointing_model_file.native()}, {"error", to_string(loaded.error())}});
    return kExitFailed;
  }
  if (loaded->station_id != settings.station_id) {
    logger.error("model_for_another_station",
                 {{"station_id", settings.station_id}, {"model_station_id", loaded->station_id}});
    return kExitFailed;
  }
  logger.info("model_loaded", {{"model_id", loaded->model_id},
                               {"sha256", loaded->sha256_hex},
                               {"size_bytes", count(loaded->size_bytes)},
                               {"terms", count(loaded->model.coefficients().size())}});
  Result<DatagramReceiver> receiver = DatagramReceiver::make(settings.tagger_address, settings.tagger_port);
  if (!receiver) {
    logger.error("tagger_socket_unavailable", {{"address", settings.tagger_address},
                                               {"port", static_cast<std::int64_t>(settings.tagger_port)},
                                               {"error", to_string(receiver.error())}});
    return kExitFailed;
  }
  // The config reader holds both axes to 2 counts a revolution or more.
  AngleTrack track = AngleTrack::make(settings.encoders, history_for(length)).value();
  logger.info("listening", {{"address", settings.tagger_address},
                            {"port", static_cast<std::int64_t>(receiver->port())},
                            {"seconds", static_cast<std::int64_t>(length.count())}});
  listen(*receiver, track, loaded->model, length, logger);
  if (!report_frames(folder, track, loaded->model, logger)) {
    return kExitFailed;
  }
  return track.stats().batches > 0 ? kExitHeard : kExitSilent;
}

}  // namespace

void report_status(const AngleTrack& track, const ReceiverStats& received, const PointingModel& model,
                   const UtcTime host_now, const logging::Logger& logger) {
  const TrackStats& stats = track.stats();
  const std::optional<AxisAngles> newest = track.newest();
  if (!newest) {
    logger.info("encoder_status", {{"datagrams", count(received.datagrams)},
                                   {"malformed", count(received.malformed)},
                                   {"batches", count(stats.batches)},
                                   {"lost_batches", count(stats.lost_batches)},
                                   {"acquiring", count(stats.acquiring)}});
    return;
  }
  const Direction sky = to_sky(model, newest->encoder);
  logger.info("encoder_status", {{"datagrams", count(received.datagrams)},
                                 {"malformed", count(received.malformed)},
                                 {"batches", count(stats.batches)},
                                 {"lost_batches", count(stats.lost_batches)},
                                 {"sequence_resets", count(stats.sequence_resets)},
                                 {"samples", count(stats.samples)},
                                 {"acquiring", count(stats.acquiring)},
                                 {"out_of_order", count(stats.out_of_order)},
                                 {"utc_ns", to_utc_ns(newest->utc)},
                                 {"clock_state", to_string(newest->state)},
                                 {"bound_ns", static_cast<std::int64_t>(newest->bound.count())},
                                 {"lag_ns", static_cast<std::int64_t>((host_now - newest->utc).count())},
                                 {"encoder_azimuth_rad", newest->encoder.azimuth_rad},
                                 {"encoder_elevation_rad", newest->encoder.elevation_rad},
                                 {"sky_azimuth_rad", sky.azimuth_rad},
                                 {"sky_elevation_rad", sky.elevation_rad}});
}

void report_frame(const AngleTrack& track, const PointingModel& model, const v1::CameraFrameMeta& frame,
                  const logging::Logger& logger) {
  const Result<FrameLineOfSight> seen = line_of_sight(track, model, frame);
  if (!seen) {
    logger.warn("frame_refused", {{"camera_id", frame.camera_id()},
                                  {"segment_id", frame.segment_id()},
                                  {"frame_index", count(frame.frame_index())},
                                  {"error", to_string(seen.error())}});
    return;
  }
  logger.info("frame_line_of_sight", {{"camera_id", frame.camera_id()},
                                      {"segment_id", frame.segment_id()},
                                      {"frame_index", count(seen->frame_index)},
                                      {"mid_exposure_utc_ns", to_utc_ns(seen->mid_exposure)},
                                      {"sky_azimuth_rad", seen->sky.azimuth_rad},
                                      {"sky_elevation_rad", seen->sky.elevation_rad},
                                      {"encoder_azimuth_rad", seen->axes.encoder.azimuth_rad},
                                      {"encoder_elevation_rad", seen->axes.encoder.elevation_rad},
                                      {"azimuth_rate_rad_per_s", seen->axes.azimuth_rate_rad_per_s},
                                      {"elevation_rate_rad_per_s", seen->axes.elevation_rate_rad_per_s},
                                      {"bound_rad", seen->axes.bound_rad},
                                      {"time_bound_ns", static_cast<std::int64_t>(seen->axes.time_bound.count())},
                                      {"clock_state", to_string(seen->axes.state)}});
}

void listen(DatagramReceiver& receiver, AngleTrack& track, const PointingModel& model,
            const std::chrono::seconds length, const logging::Logger& logger) {
  const steady_clock::time_point until = steady_clock::now() + length;
  steady_clock::time_point next_report = steady_clock::now() + kReportEvery;
  for (steady_clock::time_point now = steady_clock::now(); now < until; now = steady_clock::now()) {
    const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(std::min(until, next_report) - now);
    const Result<tagger::Batch> batch = receiver.receive(wait);
    if (batch) {
      track.add(*batch);
    }
    if (steady_clock::now() >= next_report) {
      report_status(track, receiver.stats(), model,
                    std::chrono::time_point_cast<Duration>(std::chrono::system_clock::now()), logger);
      next_report += kReportEvery;
    }
  }
  report_status(track, receiver.stats(), model, std::chrono::time_point_cast<Duration>(std::chrono::system_clock::now()),
                logger);
}

bool report_frames(const std::filesystem::path& folder, const AngleTrack& track, const PointingModel& model,
                   const logging::Logger& logger) {
  std::error_code error;
  std::vector<std::filesystem::path> files;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error)) {
    if (entry.path().extension() == ".meta") {
      files.push_back(entry.path());
    }
  }
  std::ranges::sort(files);
  for (const std::filesystem::path& file : files) {
    const Result<std::vector<v1::CameraFrameMeta>> frames = read_frame_meta_file(file);
    if (!frames) {
      logger.error("meta_unreadable", {{"file", file.filename().native()}, {"error", to_string(frames.error())}});
      return false;
    }
    for (const v1::CameraFrameMeta& frame : *frames) {
      report_frame(track, model, frame, logger);
    }
  }
  return true;
}

int run(const std::span<const char* const> args, const std::filesystem::path& config_path,
        const std::filesystem::path& folder) {
  const std::optional<std::chrono::seconds> length =
      args.size() == 2 ? listen_seconds(args[1]) : std::optional<std::chrono::seconds>();
  if (!length) {
    std::fprintf(stderr,
                 "usage: ics-encoder-monitor SECONDS\nIt listens for the encoder time-tagger for 1 to %lld s, then "
                 "gives the line of sight of each frame in the .meta files in the working folder; the config file "
                 "is %s.\n",
                 static_cast<long long>(kMaxListenSeconds), config_path.c_str());
    return kExitUsage;
  }
  const auto config = config::read_file(config_path, &read_monitor_config);
  if (!config) {
    std::fputs(config::format_errors(config_path.native(), config.error()).c_str(), stderr);
    return kExitUsage;
  }
  const logging::Logger logger = logging::Logger::to_stderr(config->log.service, config->log.level);
  return monitor(*config, *length, folder, logger);
}

}  // namespace ics::encoder
