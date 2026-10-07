#pragma once

#include <cstdint>
#include <filesystem>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/time_quality.pb.h"

namespace ics::camera {

// The settings ICS applies to a camera, and that the camera reports back as
// it applied them.
struct CameraSettings {
  // The image size, in pixels.
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // Where the sensor window's top-left pixel sits on the sensor, in pixels
  // from its first column and first row; 0 and 0 for a full frame.
  std::uint32_t window_x = 0;
  std::uint32_t window_y = 0;
  // Frames per second, and each frame's exposure (a FLIR camera's
  // integration time).
  std::uint32_t frame_rate = 0;
  Duration exposure{};
  // The frames each segment holds, and how many of them follow its trigger.
  std::uint32_t segment_frames = 0;
  std::uint32_t post_trigger_frames = 0;
  // Whether the camera's clock follows its IRIG-B time code input.
  bool irig = false;
};

// Frames of a segment, by number counted from its trigger frame (0).
struct FrameRange {
  std::int32_t first = 0;
  std::uint32_t count = 0;
};

// A segment once its trigger came and its post-trigger frames are recorded.
struct SegmentStatus {
  std::uint32_t segment = 0;
  // When the camera saw the trigger, by its clock.
  UtcTime trigger_time{};
  FrameRange recorded{};
};

// What ICS needs from a high-speed camera that records segments into its
// memory and saves them as cine files. The emulated Phantom (ICS-027) and the
// FLIR X6980-HS (ICS-028) implement it; bindings of the cameras' SDKs will
// once ICS has them.
class SegmentCamera {
 public:
  SegmentCamera() = default;
  SegmentCamera(const SegmentCamera&) = delete;
  SegmentCamera& operator=(const SegmentCamera&) = delete;
  SegmentCamera(SegmentCamera&&) = delete;
  SegmentCamera& operator=(SegmentCamera&&) = delete;
  virtual ~SegmentCamera() = default;

  // Applies the settings, and returns them as the camera applied them.
  [[nodiscard]] virtual Result<CameraSettings> configure(const CameraSettings& settings) = 0;
  // Splits the camera's memory into segments and starts recording into the
  // first.
  [[nodiscard]] virtual Status arm(std::uint32_t segments) = 0;
  // Triggers the segment recording now, and returns once its post-trigger
  // frames are recorded; the camera then records into the next segment.
  [[nodiscard]] virtual Result<SegmentStatus> trigger() = 0;
  // Saves frames of a recorded segment as a cine file, over the camera's
  // data link.
  [[nodiscard]] virtual Status save(std::uint32_t segment, const FrameRange& frames,
                                    const std::filesystem::path& path) = 0;
};

// The station's time quality, as ics-timingd publishes it (ICS-019).
class TimeQualitySource {
 public:
  TimeQualitySource() = default;
  TimeQualitySource(const TimeQualitySource&) = delete;
  TimeQualitySource& operator=(const TimeQualitySource&) = delete;
  TimeQualitySource(TimeQualitySource&&) = delete;
  TimeQualitySource& operator=(TimeQualitySource&&) = delete;
  virtual ~TimeQualitySource() = default;

  [[nodiscard]] virtual v1::TimeQuality current() = 0;
};

}  // namespace ics::camera
