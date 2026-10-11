#include "ics/camd/offloader.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <google/protobuf/util/delimited_message_util.h>

#include "ics/camd/drivers.hpp"
#include "ics/common/check.hpp"

namespace ics::camd {
namespace {

// Writes each segment's metadata beside its cine, and lists both files.
void write_segments(CameraOutcome& outcome) {
  for (const camera::SegmentCheck& segment : outcome.report.segments) {
    const std::filesystem::path meta = meta_path(segment.path);
    outcome.files.push_back(segment.path);
    if (!write_meta(segment.meta, meta)) {
      outcome.error = Error::kUnwritable;
      return;
    }
    outcome.files.push_back(meta);
  }
}

// One camera's offload, into its folder under run_folder.
[[nodiscard]] CameraOutcome offload_camera(const Config& config, const CameraConfig& camera,
                                           const std::filesystem::path& run_folder,
                                           camera::TimeQualitySource& quality, const UtcTime now) {
  CameraOutcome outcome{.camera_id = camera.id, .error = std::nullopt, .report = {}, .files = {}};
  const std::filesystem::path folder = run_folder / camera.id;
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  if (error) {
    outcome.error = Error::kUnwritable;
    return outcome;
  }
  const std::unique_ptr<camera::SegmentCamera> opened = open_camera(camera.driver, quality, now);
  const camera::OffloadPlan plan{.station_id = config.station_id,
                                 .camera_id = camera.id,
                                 .camera_kind = camera_kind(camera.driver),
                                 .segments = camera.segments,
                                 .frames = camera.frames,
                                 .directory = folder};
  Result<camera::OffloadReport> report = camera::record_and_offload(*opened, camera.settings, plan, quality);
  if (!report) {
    outcome.error = report.error();
    return outcome;
  }
  // record_and_offload saves every segment the plan asks for.
  static_cast<void>(ics::check(report->segments.size() == camera.segments));
  outcome.report = std::move(*report);
  write_segments(outcome);
  return outcome;
}

}  // namespace

std::filesystem::path meta_path(const std::filesystem::path& cine) {
  std::filesystem::path out = cine;
  out.replace_extension(".meta");
  return out;
}

Status write_meta(const std::vector<v1::CameraFrameMeta>& meta, const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  for (const v1::CameraFrameMeta& frame : meta) {
    if (!out || !google::protobuf::util::SerializeDelimitedToOstream(frame, &out)) {
      return fail(Error::kUnwritable);
    }
  }
  out.close();
  if (!out) {
    return fail(Error::kUnwritable);
  }
  return {};
}

std::vector<CameraOutcome> offload_all(const Config& config, const std::filesystem::path& run_folder,
                                       camera::TimeQualitySource& quality, const UtcTime now) {
  std::vector<CameraOutcome> outcomes(config.cameras.size());
  {
    std::vector<std::jthread> threads;
    threads.reserve(config.cameras.size());
    for (std::size_t i = 0; i < config.cameras.size(); ++i) {
      // Each thread writes only its own camera's outcome.
      threads.emplace_back([&config, &run_folder, &quality, &outcomes, now, i] {
        outcomes[i] = offload_camera(config, config.cameras[i], run_folder, quality, now);
      });
    }
  }
  // Each jthread joined as the block ended.
  static_cast<void>(ics::check(outcomes.size() == config.cameras.size()));
  return outcomes;
}

}  // namespace ics::camd
