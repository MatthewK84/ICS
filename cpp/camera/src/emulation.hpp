#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ics/camera/segment_camera.hpp"
#include "ics/camera/strobe.hpp"
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

// Whether a scene suits a camera with these settings: no strobe (an empty
// rectangle), or a valid schedule, a rectangle within the image, and no
// negative background, gain or noise.
[[nodiscard]] bool fits_scene(const StrobeScene& scene, const CameraSettings& settings) noexcept;

// The pixels of a frame whose exposure truly starts at start, as the scene
// lights them: little-endian 16-bit words, row by row, each rounded and held
// to 0 to max_value. key makes each frame's noise its own.
[[nodiscard]] std::vector<std::byte> render(const StrobeScene& scene, const CameraSettings& settings, UtcTime start,
                                            std::uint32_t max_value, std::uint64_t key);

// A key for a frame's noise: its segment and number.
[[nodiscard]] std::uint64_t frame_key(std::uint32_t segment, std::int32_t number) noexcept;

}  // namespace ics::camera::detail
