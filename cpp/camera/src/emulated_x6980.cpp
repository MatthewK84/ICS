#include "ics/camera/emulated_x6980.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "emulation.hpp"
#include "ics/camera/flir_sdk.hpp"
#include "ics/camera/irig.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {
namespace {

// The sensor fills 14 bits of each 16-bit pixel.
constexpr std::uint32_t kPixelMask = (std::uint32_t{1} << 14U) - 1;
constexpr std::size_t kPixelBytes = 2;
constexpr unsigned kBitsPerByte = 8;
constexpr std::uint32_t kByteMask = 0xFFU;

[[nodiscard]] bool on_sensor(const CameraSettings& s) noexcept {
  const std::uint64_t right = std::uint64_t{s.window_x} + s.width;
  const std::uint64_t bottom = std::uint64_t{s.window_y} + s.height;
  return s.width > 0 && s.height > 0 && right <= kX6980SensorWidth && bottom <= kX6980SensorHeight;
}

// A frame's pixels: every one its number's low 14 bits.
[[nodiscard]] std::vector<std::byte> pixels(const CameraSettings& s, const std::int32_t number) {
  const std::uint32_t value = static_cast<std::uint32_t>(number) & kPixelMask;
  std::vector<std::byte> out(std::size_t{s.width} * s.height * kPixelBytes);
  for (std::size_t i = 0; i < out.size(); i += kPixelBytes) {
    out[i] = static_cast<std::byte>(value & kByteMask);
    out[i + 1] = static_cast<std::byte>((value >> kBitsPerByte) & kByteMask);
  }
  return out;
}

}  // namespace

Result<CameraSettings> EmulatedX6980::configure(const CameraSettings& settings) {
  if (!on_sensor(settings) || !detail::records(settings, schedule_)) {
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
                     .stamp = irig_from_utc(status.trigger_time, stamps_year_),
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
    const UtcTime time = recorded_[segment].trigger_time + detail::frames(number, settings_.frame_rate);
    out.push_back(FlirFrame{.number = number,
                            .stamp = irig_from_utc(time, stamps_year_),
                            .integration = settings_.exposure,
                            .pixels = pixels(settings_, number)});
  }
  return out;
}

}  // namespace ics::camera
