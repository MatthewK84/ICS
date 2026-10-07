#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "ics/camera/segment_camera.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/error.hpp"

namespace ics::camera {

// A Phantom in software, for the tests and the bench until ICS has the SDK.
// Its clock follows IRIG-B exactly when the settings turn IRIG on; each
// frame's exposure starts at its trigger's time plus its number of frame
// periods, and each segment holds the frames up to its trigger and after it.
// The scene's strobe lights its images, 12 of each pixel's 16 bits, and its
// clock stamps each frame and trigger the scene's stamp offset late; with no
// scene, images are zeros and stamps exact. It reads the full sensor's
// position: whatever window offset is asked, it applies 0 and 0, and reports
// so.
class EmulatedPhantom final : public SegmentCamera {
 public:
  explicit EmulatedPhantom(const TriggerSchedule& schedule) noexcept : schedule_(schedule) {}
  EmulatedPhantom(const TriggerSchedule& schedule, const StrobeScene& scene) noexcept
      : schedule_(schedule), scene_(scene) {}

  // Fails with Error::kInvalidArgument for a size outside 1 to 65,535 pixels,
  // a scene that does not suit the settings, no frame rate, an exposure not shorter than a frame period, a segment of
  // no frames or more than 2^20, more post-trigger frames than it holds, or a
  // schedule whose segments would overlap.
  [[nodiscard]] Result<CameraSettings> configure(const CameraSettings& settings) override;
  // Fails with Error::kInvalidArgument before configure, or for no segments
  // or more than 64.
  [[nodiscard]] Status arm(std::uint32_t segments) override;
  // Fails with Error::kFull when every armed segment is recorded, and with
  // Error::kInvalidArgument before arm.
  [[nodiscard]] Result<SegmentStatus> trigger() override;
  // Fails with Error::kOutOfRange for a segment not recorded or frames it
  // does not hold, and with Error::kUnwritable when the file cannot be
  // written.
  [[nodiscard]] Status save(std::uint32_t segment, const FrameRange& frames,
                            const std::filesystem::path& path) override;

 private:
  TriggerSchedule schedule_;
  StrobeScene scene_{};
  CameraSettings settings_{};
  bool configured_ = false;
  std::uint32_t armed_ = 0;
  std::vector<SegmentStatus> recorded_;
};

}  // namespace ics::camera
