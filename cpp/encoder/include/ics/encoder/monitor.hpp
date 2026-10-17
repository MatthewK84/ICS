#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <span>

#include "ics/common/units.hpp"
#include "ics/encoder/pointing_model.hpp"
#include "ics/encoder/receiver.hpp"
#include "ics/encoder/track.hpp"
#include "ics/logging/logger.hpp"
#include "ics/v1/camera_frame_meta.pb.h"

namespace ics::encoder {

// ics-encoder-monitor (ICS-036), the operational-test tool for the encoder
// path: it listens for the real tagger's datagrams, reports what it hears
// once a second, then gives the line of sight of each frame in the camera
// .meta files in its working folder. See cpp/README.md.

// Its exit codes.
inline constexpr int kExitHeard = 0;   // It heard the tagger, and read every .meta file.
inline constexpr int kExitFailed = 1;  // The model file, the socket or a .meta file could not be used.
inline constexpr int kExitUsage = 2;   // A bad command line or config file.
inline constexpr int kExitSilent = 3;  // No tagger batch came.

// The longest it listens; it keeps every sample heard, 24 MB at the most.
inline constexpr std::int64_t kMaxListenSeconds = 600;

// Logs "encoder_status": the track's and receiver's counts, and the newest
// sample's time, clock state, bound, axis angles and line of sight, with how
// far it lags host_now, which includes the network's delay.
void report_status(const AngleTrack& track, const ReceiverStats& received, const PointingModel& model,
                   UtcTime host_now, const logging::Logger& logger);

// Logs "frame_line_of_sight" for a frame with one, or "frame_refused" with
// the reason.
void report_frame(const AngleTrack& track, const PointingModel& model, const v1::CameraFrameMeta& frame,
                  const logging::Logger& logger);

// Adds each batch the receiver gets for length to track, and reports the
// status once a second and at the end.
void listen(DatagramReceiver& receiver, AngleTrack& track, const PointingModel& model, std::chrono::seconds length,
            const logging::Logger& logger);

// Reports each frame of each .meta file directly in folder; false, after
// logging "meta_unreadable", when one cannot be read.
[[nodiscard]] bool report_frames(const std::filesystem::path& folder, const AngleTrack& track,
                                 const PointingModel& model, const logging::Logger& logger);

// The command: SECONDS, the config file at config_path, the .meta files in
// folder.
[[nodiscard]] int run(std::span<const char* const> args, const std::filesystem::path& config_path,
                      const std::filesystem::path& folder);

}  // namespace ics::encoder
