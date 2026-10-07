#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "ics/camera/flir_sdk.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {

// A FLIR X6980-HS as a SegmentCamera (ICS-028). It drives the camera through
// its SDK, converts each IRIG-B time stamp to UTC, and saves a segment's
// frames as a cine of 16-bit pixels, 14 bits of them significant. A
// trigger's stamp takes a missing year from the station's UTC, as the time
// quality last sampled it (TimeQuality.time_utc_ns), and a frame's from its
// segment's trigger.
class FlirCamera final : public SegmentCamera {
 public:
  FlirCamera(FlirSdk& sdk, TimeQualitySource& quality) noexcept : sdk_(sdk), quality_(quality) {}

  // Fails as the SDK does.
  [[nodiscard]] Result<CameraSettings> configure(const CameraSettings& settings) override;
  // Fails as the SDK does.
  [[nodiscard]] Status arm(std::uint32_t segments) override;
  // Fails as the SDK does; with Error::kUnavailable for a stamp without its
  // year before the time quality has sampled the station's UTC; and with
  // Error::kMalformed for a stamp that is not a time or a segment out of
  // turn.
  [[nodiscard]] Result<SegmentStatus> trigger() override;
  // Fails with Error::kOutOfRange for a segment not triggered since the last
  // configure or arm; as the SDK does; with Error::kMalformed for frames
  // other than those asked, pixels of another size or a stamp that is not a
  // time; with write_cine's errors; and with Error::kUnwritable when the file
  // cannot be written.
  [[nodiscard]] Status save(std::uint32_t segment, const FrameRange& frames,
                            const std::filesystem::path& path) override;

 private:
  FlirSdk& sdk_;
  TimeQualitySource& quality_;
  CameraSettings settings_{};
  // Each segment's trigger, in UTC, by segment number.
  std::vector<UtcTime> triggers_;
};

}  // namespace ics::camera
