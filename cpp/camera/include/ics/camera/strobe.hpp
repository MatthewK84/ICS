#pragma once

#include <cstdint>

#include "ics/common/units.hpp"

namespace ics::camera {

// The station's PPS strobe (ICS-029), as the emulated cameras see it and the
// strobe analyzer models it.

// A rectangle of an image, in pixels: its first column, its first row as the
// image stores its rows, and its size.
struct Roi {
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

// When the strobe lights: for pulse_width, from each UTC second's start plus
// latency (the strobe's own delay after the PPS edge) plus a sweep delay. The
// sweep delay is delay_step times the second's count since 1970 modulo
// sweep_steps, so it steps by delay_step each second and repeats; a
// sweep_steps of 1 is no sweep.
struct StrobeSchedule {
  Duration pulse_width{};
  Duration latency{};
  Duration delay_step{};
  std::uint32_t sweep_steps = 1;

  // When the pulse of the UTC second that starts second seconds after 1970
  // starts.
  [[nodiscard]] UtcTime pulse_start(std::int64_t second) const noexcept;
  // Whether a strobe keeps it: a pulse, no negative delay, 1 to 3,600 sweep
  // steps, and every pulse over within its second.
  [[nodiscard]] bool valid() const noexcept;
};

// The strobe light within an exposure, from the pulses of the second it
// starts in and the next, for a valid schedule and an exposure under 1 s.
[[nodiscard]] Duration strobe_light(const StrobeSchedule& schedule, UtcTime exposure_start, Duration exposure) noexcept;

// What an emulated camera sees of the strobe, and how late its clock stamps
// each frame.
struct StrobeScene {
  StrobeSchedule schedule{};
  // Where the strobe's light falls; an empty rectangle is no strobe.
  Roi roi{};
  // How much later than its exposure truly starts the camera stamps each
  // frame: the offset the strobe analyzer measures.
  Duration stamp_offset{};
  // Each pixel's value, in counts: background everywhere, and in the
  // rectangle gain_per_us for each microsecond of strobe light within the
  // frame's exposure, plus Gaussian noise of noise_sigma, from seed.
  double background = 0.0;
  double gain_per_us = 0.0;
  double noise_sigma = 0.0;
  std::uint64_t seed = 0;
};

}  // namespace ics::camera
