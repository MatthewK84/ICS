#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/v1/time_quality.pb.h"

namespace ics::timing {

// The camera offsets a station's strobe calibration measured (ICS-029). The
// file holds a serialized ics.v1.TimeQuality with only station_id and
// camera_offsets set; ics-timingd copies the offsets into each report.
struct CameraOffsets {
  std::string station_id;
  std::vector<v1::TimeQuality::CameraOffset> offsets;
};

// The largest offsets file read.
inline constexpr std::size_t kMaxCameraOffsetsBytes = std::size_t{1} << 20U;

// Parses an offsets file's bytes. Fails with Error::kMalformed for more than
// kMaxCameraOffsetsBytes, bytes that are not a TimeQuality, a TimeQuality
// with fields other than station_id and camera_offsets, no station_id, or an
// offset with no camera_id, a negative sigma or no measured time.
[[nodiscard]] Result<CameraOffsets> parse_camera_offsets(std::span<const std::byte> bytes);

// Reads and parses an offsets file. Fails with Error::kEmpty for a file that
// does not exist, Error::kUnreadable for one that cannot be read, and as
// parse_camera_offsets does.
[[nodiscard]] Result<CameraOffsets> read_camera_offsets(const std::filesystem::path& file);

// Writes an offsets file whole or not at all: to a temporary file beside it,
// synced, then renamed over it. Fails with Error::kUnwritable.
[[nodiscard]] Status write_camera_offsets(const std::filesystem::path& file, const CameraOffsets& offsets);

// The offsets with offset in place of the same camera's, or added.
[[nodiscard]] CameraOffsets with_offset(CameraOffsets offsets, const v1::TimeQuality::CameraOffset& offset);

// Watches an offsets file for ics-timingd, which polls it with each report.
class CameraOffsetsWatcher {
 public:
  explicit CameraOffsetsWatcher(std::filesystem::path file) : file_(std::move(file)) {}

  // Nothing when the file's modification time, or its absence, is as at the
  // last poll; otherwise what the file now holds, or why it cannot be used.
  // The first poll always answers. An unchanged poll does not allocate.
  [[nodiscard]] std::optional<Result<CameraOffsets>> poll();

 private:
  std::filesystem::path file_;
  bool polled_ = false;
  // When the file was last modified, or nothing for no file.
  std::optional<std::filesystem::file_time_type> modified_;
};

}  // namespace ics::timing
