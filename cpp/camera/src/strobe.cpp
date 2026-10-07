#include "ics/camera/strobe.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>

#include "ics/common/units.hpp"

namespace ics::camera {
namespace {

using std::chrono::seconds;

constexpr std::uint32_t kMaxSweepSteps = 3600;

// The time two intervals overlap.
[[nodiscard]] Duration overlap(const UtcTime a, const Duration a_length, const UtcTime b,
                               const Duration b_length) noexcept {
  const UtcTime start = std::max(a, b);
  const UtcTime end = std::min(a + a_length, b + b_length);
  return std::max(end - start, Duration::zero());
}

}  // namespace

UtcTime StrobeSchedule::pulse_start(const std::int64_t second) const noexcept {
  const std::int64_t steps = std::max<std::int64_t>(sweep_steps, 1);
  const std::int64_t step = ((second % steps) + steps) % steps;
  return UtcTime(seconds(second)) + latency + (delay_step * step);
}

bool StrobeSchedule::valid() const noexcept {
  const bool sized = pulse_width > Duration::zero() && latency >= Duration::zero() &&
                     delay_step >= Duration::zero() && sweep_steps >= 1 && sweep_steps <= kMaxSweepSteps;
  return sized && latency + (delay_step * (sweep_steps - 1)) + pulse_width < seconds(1);
}

Duration strobe_light(const StrobeSchedule& schedule, const UtcTime exposure_start, const Duration exposure) noexcept {
  const std::int64_t second = std::chrono::floor<seconds>(exposure_start).time_since_epoch().count();
  return overlap(exposure_start, exposure, schedule.pulse_start(second), schedule.pulse_width) +
         overlap(exposure_start, exposure, schedule.pulse_start(second + 1), schedule.pulse_width);
}

}  // namespace ics::camera
