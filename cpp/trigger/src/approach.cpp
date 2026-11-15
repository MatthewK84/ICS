// approach.cpp: when an interceptor is closest to its target (ICS-040).
// JSF deviation D-6: ics::check, ics::Duration and ics::UtcTime come from .hpp headers.
#include <cmath>
#include <cstddef>
#include <optional>

#include <ics/common/check.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>
#include <ics/trigger/approach.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::cue::Vector3;

constexpr float64 nanoseconds_per_second = 1.0E9;
constexpr int32 newton_steps = 8;

// The interceptor as the target sees it.
struct Closing
{
  // The interceptor's position from the target, in metres.
  Vector3 position;
  // The target's velocity less the interceptor's, in m/s.
  Vector3 velocity;
  // The target's acceleration less the interceptor's, in m/s^2.
  Vector3 acceleration;
  // The interceptor's own speed, squared.
  float64 speed_squared;
};

Closing closing_of(const ics::cue::Estimate& target, const ics::cue::Estimate& interceptor)
{
  Closing closing{};
  for (int32 axis = 0; axis < ics::cue::axes; ++axis)
  {
    const std::size_t place = static_cast<std::size_t>(axis);
    const std::size_t rate = static_cast<std::size_t>(ics::cue::index(1, axis));
    const std::size_t push = static_cast<std::size_t>(ics::cue::index(2, axis));
    closing.position[place] = interceptor.state[place] - target.state[place];
    closing.velocity[place] = target.state[rate] - interceptor.state[rate];
    closing.acceleration[place] = target.state[push] - interceptor.state[push];
    closing.speed_squared += interceptor.state[rate] * interceptor.state[rate];
  }
  return closing;
}

// first . second.
float64 dot(const Vector3& first, const Vector3& second)
{
  float64 sum = 0.0;
  for (int32 axis = 0; axis < ics::cue::axes; ++axis)
  {
    const std::size_t place = static_cast<std::size_t>(axis);
    sum += first[place] * second[place];
  }
  return sum;
}

// The interceptor's position from the target after some seconds, with the
// closing acceleration scaled: 1 keeps it, 0 leaves the velocity alone.
Vector3 separation_after(const Closing& closing,
                         const float64 seconds,
                         const float64 scale)
{
  const float64 half = (seconds * seconds) / 2.0;
  Vector3 apart{};
  for (int32 axis = 0; axis < ics::cue::axes; ++axis)
  {
    const std::size_t place = static_cast<std::size_t>(axis);
    const float64 coasted = closing.position[place] - (closing.velocity[place] * seconds);
    apart[place] = coasted - ((closing.acceleration[place] * scale) * half);
  }
  return apart;
}

// How fast the separation changes after some seconds.
Vector3 rate_after(const Closing& closing, const float64 seconds)
{
  Vector3 rate{};
  for (int32 axis = 0; axis < ics::cue::axes; ++axis)
  {
    const std::size_t place = static_cast<std::size_t>(axis);
    rate[place] = (-closing.velocity[place]) - (closing.acceleration[place] * seconds);
  }
  return rate;
}

// A time some seconds after another, to the nearest nanosecond.
ics::UtcTime after(const ics::UtcTime time, const float64 seconds)
{
  return time + ics::Duration(static_cast<ics::int64>(std::floor((seconds * nanoseconds_per_second) + 0.5)));
}

}  // namespace

namespace ics::trigger
{

bool valid(const Approach_settings& settings)
{
  const bool launching = std::isfinite(settings.launch_speed_m_s) && (settings.launch_speed_m_s > 0.0);
  return launching && (settings.horizon > Duration::zero());
}

std::optional<Approach> coarse_approach(const cue::Estimate& target,
                                        const cue::Estimate& interceptor,
                                        const Approach_settings& settings)
{
  const Closing closing = closing_of(target, interceptor);
  const float64 gained = dot(closing.position, closing.velocity);
  const bool launched = closing.speed_squared >= (settings.launch_speed_m_s * settings.launch_speed_m_s);
  std::optional<Approach> approach;
  if (check(target.time == interceptor.time) && launched && (gained > 0.0))
  {
    // The two are closing, so the closing velocity is not zero.
    const float64 seconds = gained / dot(closing.velocity, closing.velocity);
    if ((seconds * nanoseconds_per_second) < static_cast<float64>(settings.horizon.count()))
    {
      const Vector3 apart = separation_after(closing, seconds, 0.0);
      approach = Approach{after(target.time, seconds), std::sqrt(dot(apart, apart))};
    }
  }
  return approach;
}

std::optional<Approach> refined_approach(const cue::Estimate& target,
                                         const cue::Estimate& interceptor,
                                         const Approach_settings& settings)
{
  std::optional<Approach> approach = coarse_approach(target, interceptor, settings);
  if (approach.has_value())
  {
    const Closing closing = closing_of(target, interceptor);
    float64 seconds = static_cast<float64>((approach->time - target.time).count()) / nanoseconds_per_second;
    bool usable = true;
    for (int32 step = 0; step < newton_steps; ++step)
    {
      const Vector3 apart = separation_after(closing, seconds, 1.0);
      const Vector3 rate = rate_after(closing, seconds);
      const float64 slope = dot(rate, rate) - dot(apart, closing.acceleration);
      if (usable && (slope > 0.0))
      {
        seconds = seconds - (dot(apart, rate) / slope);
      }
      else
      {
        usable = false;
      }
    }
    // Ahead of now and inside the horizon: within half the horizon of its middle.
    const float64 middle = static_cast<float64>(settings.horizon.count()) / 2.0;
    if (usable && (std::abs((seconds * nanoseconds_per_second) - middle) < middle))
    {
      const Vector3 apart = separation_after(closing, seconds, 1.0);
      approach = Approach{after(target.time, seconds), std::sqrt(dot(apart, apart))};
    }
  }
  return approach;
}

}  // namespace ics::trigger
