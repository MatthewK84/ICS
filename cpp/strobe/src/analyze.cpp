#include "ics/strobe/analyze.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "ics/camera/cine.hpp"
#include "ics/camera/mapped_file.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/strobe/detect.hpp"
#include "ics/strobe/fit.hpp"
#include "ics/timing/camera_offsets.hpp"
#include "ics/v1/time_quality.pb.h"

namespace ics::strobe {
namespace {

constexpr std::int64_t kNsPerSecond = 1'000'000'000;

// The cine files in a folder, in name order.
[[nodiscard]] Result<std::vector<std::filesystem::path>> cines_in(const std::filesystem::path& folder) {
  std::error_code error;
  std::filesystem::directory_iterator entry(folder, error);
  std::vector<std::filesystem::path> out;
  for (; !error && entry != std::filesystem::directory_iterator(); entry.increment(error)) {
    if (entry->path().extension() == ".cine") {
      out.push_back(entry->path());
    }
  }
  if (error) {
    return fail(Error::kUnreadable);
  }
  std::ranges::sort(out);
  return out;
}

[[nodiscard]] Result<StrobeSegment> segment_of(const std::filesystem::path& path, const camera::Roi& roi) {
  const Result<camera::MappedFile> file = camera::MappedFile::open(path);
  const Result<camera::Cine> cine = file ? camera::read_cine(file->bytes()) : fail(file.error());
  return cine ? read_segment(file->bytes(), *cine, roi) : fail(cine.error());
}

}  // namespace

Result<StrobeReport> analyze(const std::span<const StrobeSegment> segments, const StrobeSettings& settings) {
  StrobeReport report{.offset = {}, .fit = {}, .segments = segments.size(), .lit = 0, .unlit = 0, .scattered = 0};
  std::vector<StrobeFrame> frames;
  std::int64_t latest = 0;
  for (const StrobeSegment& segment : segments) {
    const bool used = segment.lit > 0 && segment.contiguous;
    report.lit += used ? 1 : 0;
    report.unlit += segment.lit == 0 ? 1 : 0;
    report.scattered += segment.lit > 0 && !segment.contiguous ? 1 : 0;
    if (used) {
      latest = std::max(latest, segment.second);
      frames.insert(frames.end(), segment.frames.begin(), segment.frames.end());
    }
  }
  if (report.lit == 0) {
    return fail(Error::kEmpty);
  }
  const Result<OffsetFit> fit = fit_offset(frames, settings.schedule, settings.max_offset);
  if (!fit) {
    return fail(fit.error());
  }
  report.fit = *fit;
  report.offset.set_camera_id(settings.camera_id);
  report.offset.set_offset_ns(fit->offset.count());
  report.offset.set_offset_sigma_ns(fit->sigma.count());
  report.offset.set_measured_utc_ns(latest * kNsPerSecond);
  return report;
}

Result<StrobeReport> analyze_folder(const std::filesystem::path& folder, const StrobeSettings& settings) {
  const Result<std::vector<std::filesystem::path>> paths = cines_in(folder);
  if (!paths) {
    return fail(paths.error());
  }
  if (paths->empty()) {
    return fail(Error::kEmpty);
  }
  std::vector<StrobeSegment> segments;
  for (const std::filesystem::path& path : *paths) {
    Result<StrobeSegment> segment = segment_of(path, settings.roi);
    if (!segment) {
      return fail(segment.error());
    }
    segments.push_back(std::move(*segment));
  }
  return analyze(segments, settings);
}

Status publish(const v1::TimeQuality::CameraOffset& offset, const std::string_view station_id,
               const std::filesystem::path& file) {
  Result<timing::CameraOffsets> current = timing::read_camera_offsets(file);
  if (!current && current.error() == Error::kEmpty) {
    current = timing::CameraOffsets{.station_id = std::string(station_id), .offsets = {}};
  }
  if (!current) {
    return fail(current.error());
  }
  if (current->station_id != station_id) {
    return fail(Error::kInvalidArgument);
  }
  return timing::write_camera_offsets(file, timing::with_offset(std::move(*current), offset));
}

}  // namespace ics::strobe
