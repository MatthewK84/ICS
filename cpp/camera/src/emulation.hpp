#pragma once

#include <cstdint>

#include "ics/camera/segment_camera.hpp"
#include "ics/camera/trigger_schedule.hpp"
#include "ics/common/units.hpp"

// What the emulated cameras share: how they time frames and segments.
namespace ics::camera::detail {

// The time from a frame to the frame number frames after it, at a frame
// rate, to the nanosecond.
[[nodiscard]] Duration frames(std::int64_t number, std::uint32_t frame_rate) noexcept;

// Whether an emulated camera records with the timing and segments set: a
// frame rate, an exposure shorter than a frame period, a segment of 1 to
// 2^20 frames with no more after its trigger than it holds, and a schedule
// whose segments do not overlap.
[[nodiscard]] bool records(const CameraSettings& settings, const TriggerSchedule& schedule) noexcept;

// Whether an emulated camera arms that many segments: 1 to 64.
[[nodiscard]] bool armable(std::uint32_t segments) noexcept;

// Whether a segment holding the frames held holds the frames wanted, which
// are at least one.
[[nodiscard]] bool holds(const FrameRange& held, const FrameRange& wanted) noexcept;

// The segment numbered segment, as a camera with these settings records it
// on the schedule: its trigger and the frames around it.
[[nodiscard]] SegmentStatus scheduled(const CameraSettings& settings, const TriggerSchedule& schedule,
                                      std::uint32_t segment) noexcept;

}  // namespace ics::camera::detail
