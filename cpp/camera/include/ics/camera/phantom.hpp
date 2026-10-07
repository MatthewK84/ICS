#pragma once

#include <cstdint>
#include <filesystem>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/v1/time_quality.pb.h"

namespace ics::camera {

// The settings ICS applies to a Phantom, and that the camera reports back as
// it applied them.
struct CameraSettings {
  // The image size, in pixels.
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // Frames per second, and each frame's exposure.
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

// What ICS needs from a Phantom. An emulated camera implements it now, and a
// binding of the Phantom SDK will once ICS has the SDK (ICS-027).
class PhantomCamera {
 public:
  PhantomCamera() = default;
  PhantomCamera(const PhantomCamera&) = delete;
  PhantomCamera& operator=(const PhantomCamera&) = delete;
  PhantomCamera(PhantomCamera&&) = delete;
  PhantomCamera& operator=(PhantomCamera&&) = delete;
  virtual ~PhantomCamera() = default;

  // Applies the settings, and returns them as the camera applied them.
  [[nodiscard]] virtual Result<CameraSettings> configure(const CameraSettings& settings) = 0;
  // Splits the camera's memory into segments and starts recording into the
  // first.
  [[nodiscard]] virtual Status arm(std::uint32_t segments) = 0;
  // Triggers the segment recording now, and returns once its post-trigger
  // frames are recorded; the camera then records into the next segment.
  [[nodiscard]] virtual Result<SegmentStatus> trigger() = 0;
  // Saves frames of a recorded segment as a cine file, over the camera's
  // 10GbE link.
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
