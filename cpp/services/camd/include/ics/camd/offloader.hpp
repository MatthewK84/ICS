#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ics/camd/config.hpp"
#include "ics/camera/offload.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/camera_frame_meta.pb.h"

namespace ics::camd {

// How one camera's offload went.
struct CameraOutcome {
  std::string camera_id;
  // Why the offload failed, if it did: the camera's error, or kUnwritable
  // for a folder or metadata file that could not be written.
  std::optional<Error> error;
  // Each segment saved, and how its times verified.
  camera::OffloadReport report;
  // Each segment's cine, then its metadata file, in the order saved.
  std::vector<std::filesystem::path> files;
};

// The metadata file beside a segment's cine: cine-NNN.meta for cine-NNN.cine.
[[nodiscard]] std::filesystem::path meta_path(const std::filesystem::path& cine);

// Writes meta to path, replacing any file there, as a sequence of
// length-delimited CameraFrameMeta messages: each message's size as a
// varint, then the message. kUnwritable when it cannot.
[[nodiscard]] Status write_meta(const std::vector<v1::CameraFrameMeta>& meta, const std::filesystem::path& path);

// Records and offloads every configured camera at once, each on a thread of
// its own (ICS-031): opens it with its driver at now, makes its folder,
// run_folder/ID, runs camera::record_and_offload into it and writes each
// segment's metadata beside its cine. quality must be safe to read from
// every camera's thread. One outcome for each camera, in the config's order;
// a camera that fails does not stop the others.
[[nodiscard]] std::vector<CameraOutcome> offload_all(const Config& config, const std::filesystem::path& run_folder,
                                                     camera::TimeQualitySource& quality, UtcTime now);

}  // namespace ics::camd
