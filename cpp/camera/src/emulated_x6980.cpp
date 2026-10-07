#include "ics/camera/emulated_x6980.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "emulation.hpp"
#include "ics/camera/flir_sdk.hpp"
#include "ics/camera/irig.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/camera/strobe.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {
namespace {

// The sensor fills 14 bits of each 16-bit pixel.
constexpr std::uint32_t kMaxValue = (std::uint32_t{1} << 14U) - 1;

[[nodiscard]] bool on_sensor(const CameraSettings& s) noexcept {
  const std::uint64_t right = std::uint64_t{s.window_x} + s.width;
  const std::uint64_t bottom = std::uint64_t{s.window_y} + s.height;
  return s.width > 0 && s.height > 0 && right <= kX6980SensorWidth && bottom <= kX6980SensorHeight;
}

}  // namespace

Result<CameraSettings> EmulatedX6980::configure(const CameraSettings& settings) {
  if (!on_sensor(settings) || !detail::records(settings, schedule_) || !detail::fits_scene(scene_, settings)) {
    return fail(Error::kInvalidArgument);
  }
  settings_ = settings;
  configured_ = true;
  armed_ = 0;
  recorded_.clear();
  return settings_;
}

Status EmulatedX6980::arm(const std::uint32_t segments) {
  if (!configured_ || !detail::armable(segments)) {
    return fail(Error::kInvalidArgument);
  }
  armed_ = segments;
  recorded_.clear();
  return {};
}

Result<FlirTrigger> EmulatedX6980::trigger() {
  if (armed_ == 0) {
    return fail(Error::kInvalidArgument);
  }
  if (recorded_.size() >= armed_) {
    return fail(Error::kFull);
  }
  const SegmentStatus status = detail::scheduled(settings_, schedule_, static_cast<std::uint32_t>(recorded_.size()));
  recorded_.push_back(status);
  return FlirTrigger{.segment = status.segment,
                     .stamp = irig_from_utc(status.trigger_time + scene_.stamp_offset, stamps_year_),
                     .recorded = status.recorded};
}

Result<std::vector<FlirFrame>> EmulatedX6980::read(const std::uint32_t segment, const FrameRange& frames) {
  if (segment >= recorded_.size() || !detail::holds(recorded_[segment].recorded, frames)) {
    return fail(Error::kOutOfRange);
  }
  std::vector<FlirFrame> out;
  out.reserve(frames.count);
  for (std::uint32_t i = 0; i < frames.count; ++i) {
    const std::int32_t number = frames.first + static_cast<std::int32_t>(i);
    const UtcTime start = recorded_[segment].trigger_time + detail::frames(number, settings_.frame_rate);
    out.push_back(FlirFrame{.number = number,
                            .stamp = irig_from_utc(start + scene_.stamp_offset, stamps_year_),
                            .integration = settings_.exposure,
                            .pixels = detail::render(scene_, settings_, start, kMaxValue,
                                                     detail::frame_key(segment, number))});
  }
  return out;
}

}  // namespace ics::camera
