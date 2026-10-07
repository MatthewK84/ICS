#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "ics/camera/phantom.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {

// When the emulated camera's triggers come, by its IRIG clock: the first at
// first, and each later one interval after the one before.
struct TriggerSchedule {
  UtcTime first{};
  Duration interval{};
};

// A Phantom in software, for the tests and the bench until ICS has the SDK.
// Its clock follows IRIG-B exactly when the settings turn IRIG on; each
// frame's time is its trigger's time plus its number of frame periods, and
// each segment holds the frames up to its trigger and after it. Saved cines
// have images of zeros.
class EmulatedPhantom final : public PhantomCamera {
 public:
  explicit EmulatedPhantom(const TriggerSchedule& schedule) noexcept : schedule_(schedule) {}

  // Fails with Error::kInvalidArgument for a size outside 1 to 65,535 pixels,
  // no frame rate, an exposure not shorter than a frame period, a segment of
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
  CameraSettings settings_{};
  bool configured_ = false;
  std::uint32_t armed_ = 0;
  std::vector<SegmentStatus> recorded_;
};

}  // namespace ics::camera
