#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "ics/camera/strobe.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/strobe/detect.hpp"
#include "ics/strobe/fit.hpp"
#include "ics/v1/time_quality.pb.h"

namespace ics::strobe {

// What the analyzer needs to know of a strobe calibration (ICS-029).
struct StrobeSettings {
  std::string camera_id;
  // Where the strobe's light falls in the camera's images.
  camera::Roi roi{};
  camera::StrobeSchedule schedule{};
  // How far from 0 to search for the offset.
  Duration max_offset{};
};

// A calibration's result, and the segments behind it.
struct StrobeReport {
  v1::TimeQuality::CameraOffset offset;
  OffsetFit fit;
  std::size_t segments = 0;
  // Segments fitted: the strobe lit them, in frames that follow one another.
  std::size_t lit = 0;
  // Segments left out: no frame lit, as when a pulse falls between exposures
  // or the strobe misses a second; or lit frames apart, as from a stray light.
  std::size_t unlit = 0;
  std::size_t scattered = 0;
};

// Fits one offset to the frames of the lit segments. The offset is measured
// at the latest of their seconds. Fails with Error::kEmpty when no segment is
// lit, and as fit_offset does.
[[nodiscard]] Result<StrobeReport> analyze(std::span<const StrobeSegment> segments, const StrobeSettings& settings);

// Reads each cine file (*.cine) in a folder, in name order, as a segment,
// and analyzes them. Fails with Error::kEmpty for a folder without one, with
// Error::kUnreadable for a folder that cannot be listed, and as
// camera::MappedFile::open, camera::read_cine, read_segment and analyze do.
[[nodiscard]] Result<StrobeReport> analyze_folder(const std::filesystem::path& folder, const StrobeSettings& settings);

// Puts an offset into a station's offsets file (timing::CameraOffsets),
// replacing the camera's earlier one and keeping the other cameras'. A
// missing file (Error::kEmpty) starts empty. Fails with Error::kInvalidArgument for a file of
// another station, and as timing::read_camera_offsets and
// timing::write_camera_offsets do; a file it cannot use is left as it is.
[[nodiscard]] Status publish(const v1::TimeQuality::CameraOffset& offset, std::string_view station_id,
                             const std::filesystem::path& file);

}  // namespace ics::strobe
