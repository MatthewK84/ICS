// slew.cpp: the axis rates that take a mount to a new target (ICS-039).
// JSF deviation D-6: ics::check, ics::encoder::wrap_pi and ics::encoder::Direction come from .hpp headers.
#include <algorithm>
#include <cmath>

#include <ics/common/check.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/handoff/slew.h>
#include <ics/tracker/demand.h>
#include <ics/tracker/estimator.h>

namespace
{

using ics::float64;

constexpr float64 nanoseconds_per_second = 1.0E9;

// Whether a number is finite and positive.
bool positive(const float64 value)
{
  return std::isfinite(value) && (value > 0.0);
}

// A rate held within a limit either way.
float64 held(const float64 rate, const float64 limit)
{
  return std::min(std::max(rate, -limit), limit);
}

}  // namespace

namespace ics::handoff
{

bool valid(const Slew_limits& limits)
{
  return positive(limits.rate_rad_s) && positive(limits.acceleration_rad_s2) && positive(limits.gain);
}

float64 closing_rate(const float64 error, const Slew_limits& limits)
{
  static_cast<void>(check(valid(limits)));
  const float64 size = std::abs(error);
  const float64 knee = limits.acceleration_rad_s2 / (limits.gain * limits.gain);
  float64 rate = limits.gain * error;
  if (size > knee)
  {
    rate = std::copysign(std::sqrt((2.0 * limits.acceleration_rad_s2) * (size - (knee / 2.0))), error);
  }
  return rate;
}

tracker::Axis_rates slew_demand(const tracker::Angle_state& target,
                                const tracker::Angle_state& mount,
                                const Slew_limits& limits)
{
  const float64 lead = static_cast<float64>((target.time - mount.time).count()) / nanoseconds_per_second;
  const float64 mount_azimuth = mount.azimuth_rad + (mount.azimuth_rate_rad_s * lead);
  const float64 mount_elevation = mount.elevation_rad + (mount.elevation_rate_rad_s * lead);
  const float64 azimuth_error = encoder::wrap_pi(target.azimuth_rad - mount_azimuth);
  const float64 elevation_error = target.elevation_rad - mount_elevation;
  const float64 azimuth_rate = target.azimuth_rate_rad_s + closing_rate(azimuth_error, limits);
  const float64 elevation_rate = target.elevation_rate_rad_s + closing_rate(elevation_error, limits);
  return tracker::Axis_rates{held(azimuth_rate, limits.rate_rad_s), held(elevation_rate, limits.rate_rad_s)};
}

float64 slew_seconds(const float64 angle, const Slew_limits& limits)
{
  static_cast<void>(check(valid(limits)));
  const float64 size = std::abs(angle);
  float64 seconds = 2.0 * std::sqrt(size / limits.acceleration_rad_s2);
  if ((size * limits.acceleration_rad_s2) > (limits.rate_rad_s * limits.rate_rad_s))
  {
    seconds = (size / limits.rate_rad_s) + (limits.rate_rad_s / limits.acceleration_rad_s2);
  }
  return seconds;
}

float64 reach_seconds(const encoder::Direction& first,
                      const encoder::Direction& second,
                      const Slew_limits& limits)
{
  const float64 azimuth = slew_seconds(encoder::wrap_pi(second.azimuth_rad - first.azimuth_rad), limits);
  return std::max(azimuth, slew_seconds(second.elevation_rad - first.elevation_rad, limits));
}

}  // namespace ics::handoff
