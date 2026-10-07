#pragma once

#include <cstdint>
#include <vector>

#include "ics/camera/flir_sdk.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/error.hpp"

namespace ics::camera {

// The X6980-HS's sensor, in pixels.
inline constexpr std::uint32_t kX6980SensorWidth = 640;
inline constexpr std::uint32_t kX6980SensorHeight = 512;

// A FLIR X6980-HS's SDK in software, for the tests and the bench until ICS
// has the SDK. Its IRIG-B decoder follows UTC exactly and latches each
// frame's time to the microsecond, with the year when stamps_year is set, as
// an IRIG source sending IEEE 1344 control functions makes it. Each frame's
// exposure starts at its trigger's time plus its number of frame periods.
// The scene's strobe lights its pixels, 14 of each pixel's 16 bits, and its
// decoder stamps each frame and trigger the scene's stamp offset late; with
// no scene, pixels are zeros and stamps exact.
class EmulatedX6980 final : public FlirSdk {
 public:
  EmulatedX6980(const TriggerSchedule& schedule, const bool stamps_year) noexcept
      : schedule_(schedule), stamps_year_(stamps_year) {}
  EmulatedX6980(const TriggerSchedule& schedule, const bool stamps_year, const StrobeScene& scene) noexcept
      : schedule_(schedule), stamps_year_(stamps_year), scene_(scene) {}

  // Fails with Error::kInvalidArgument for a window not on the sensor, a
  // scene that does not suit the settings, no frame rate, an integration time not shorter than a frame period, a
  // segment of no frames or more than 2^20, more post-trigger frames than it
  // holds, or a schedule whose segments would overlap.
  [[nodiscard]] Result<CameraSettings> configure(const CameraSettings& settings) override;
  // Fails with Error::kInvalidArgument before configure, or for no segments
  // or more than 64.
  [[nodiscard]] Status arm(std::uint32_t segments) override;
  // Fails with Error::kFull when every armed segment is recorded, and with
  // Error::kInvalidArgument before arm.
  [[nodiscard]] Result<FlirTrigger> trigger() override;
  // Fails with Error::kOutOfRange for a segment not recorded or frames it
  // does not hold.
  [[nodiscard]] Result<std::vector<FlirFrame>> read(std::uint32_t segment, const FrameRange& frames) override;

 private:
  TriggerSchedule schedule_;
  bool stamps_year_;
  StrobeScene scene_{};
  CameraSettings settings_{};
  bool configured_ = false;
  std::uint32_t armed_ = 0;
  std::vector<SegmentStatus> recorded_;
};

}  // namespace ics::camera
