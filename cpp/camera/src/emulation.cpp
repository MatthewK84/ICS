#include "emulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ics/camera/segment_camera.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/check.hpp"
#include "ics/common/units.hpp"

namespace ics::camera::detail {
namespace {

constexpr double kNsPerSecond = 1e9;
constexpr std::uint32_t kMaxSegmentFrames = std::uint32_t{1} << 20U;
constexpr std::uint32_t kMaxSegments = 64;

}  // namespace

Duration frames(const std::int64_t number, const std::uint32_t frame_rate) noexcept {
  // Callers check the frame rate before timing frames by it.
  static_cast<void>(check(frame_rate > 0));
  return Duration(std::llround(static_cast<double>(number) * kNsPerSecond / frame_rate));
}

bool records(const CameraSettings& settings, const TriggerSchedule& schedule) noexcept {
  const bool timing = settings.frame_rate > 0 && settings.exposure > Duration::zero() &&
                      settings.exposure < frames(1, settings.frame_rate);
  const bool segment = std::clamp(settings.segment_frames, 1U, kMaxSegmentFrames) == settings.segment_frames &&
                       settings.post_trigger_frames <= settings.segment_frames;
  return timing && segment && schedule.interval >= frames(settings.segment_frames, settings.frame_rate);
}

bool armable(const std::uint32_t segments) noexcept { return std::clamp(segments, 1U, kMaxSegments) == segments; }

bool holds(const FrameRange& held, const FrameRange& wanted) noexcept {
  const std::int64_t end = std::int64_t{wanted.first} + wanted.count;
  return wanted.count > 0 && wanted.first >= held.first && end <= std::int64_t{held.first} + held.count;
}

SegmentStatus scheduled(const CameraSettings& settings, const TriggerSchedule& schedule,
                        const std::uint32_t segment) noexcept {
  // Settings an emulated camera records keep the post-trigger frames within
  // the segment.
  static_cast<void>(check(settings.post_trigger_frames <= settings.segment_frames));
  const std::uint32_t before = settings.segment_frames - settings.post_trigger_frames;
  return SegmentStatus{.segment = segment,
                       .trigger_time = schedule.first + (schedule.interval * segment),
                       .recorded = {.first = -static_cast<std::int32_t>(before), .count = settings.segment_frames}};
}

}  // namespace ics::camera::detail
