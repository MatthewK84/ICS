// approach.cpp: when an interceptor is closest to its target (ICS-039).
// JSF deviation D-6: ics::check, ics::Duration and ics::UtcTime come from .hpp headers.
#include <cmath>
#include <cstddef>
#include <optional>

#include <ics/common/check.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>
#include <ics/handoff/approach.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::cue::Vector3;

constexpr float64 nanoseconds_per_second = 1.0E9;

// The interceptor as the target sees it.
struct Relative
{
  // The interceptor's position from the target, in metres.
  Vector3 position;
  // The target's velocity less the interceptor's, in m/s.
  Vector3 closing;
  // The interceptor's own speed, squared.
  float64 speed_squared;
};

Relative relative_of(const ics::cue::Estimate& target, const ics::cue::Estimate& interceptor)
{
  Relative relative{};
  for (int32 axis = 0; axis < ics::cue::axes; ++axis)
  {
    const std::size_t place = static_cast<std::size_t>(axis);
    const std::size_t rate = static_cast<std::size_t>(ics::cue::index(1, axis));
    relative.position[place] = interceptor.state[place] - target.state[place];
    relative.closing[place] = target.state[rate] - interceptor.state[rate];
    relative.speed_squared += interceptor.state[rate] * interceptor.state[rate];
  }
  return relative;
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

// How far apart the two are after some seconds, in metres.
float64 miss_after(const Relative& relative, const float64 seconds)
{
  Vector3 apart{};
  for (int32 axis = 0; axis < ics::cue::axes; ++axis)
  {
    const std::size_t place = static_cast<std::size_t>(axis);
    apart[place] = relative.position[place] - (relative.closing[place] * seconds);
  }
  return std::sqrt(dot(apart, apart));
}

}  // namespace

namespace ics::handoff
{

bool valid(const Approach_settings& settings)
{
  const bool launching = std::isfinite(settings.launch_speed_m_s) && (settings.launch_speed_m_s > 0.0);
  return launching && (settings.horizon > Duration::zero());
}

std::optional<Approach> closest_approach(const cue::Estimate& target,
                                         const cue::Estimate& interceptor,
                                         const Approach_settings& settings)
{
  const Relative relative = relative_of(target, interceptor);
  const float64 gained = dot(relative.position, relative.closing);
  const bool launched = relative.speed_squared >= (settings.launch_speed_m_s * settings.launch_speed_m_s);
  std::optional<Approach> approach;
  if (check(target.time == interceptor.time) && launched && (gained > 0.0))
  {
    // The two are closing, so the closing velocity is not zero.
    const float64 seconds = gained / dot(relative.closing, relative.closing);
    const float64 nanoseconds = seconds * nanoseconds_per_second;
    if (nanoseconds < static_cast<float64>(settings.horizon.count()))
    {
      const Duration ahead(static_cast<int64>(std::floor(nanoseconds + 0.5)));
      approach = Approach{target.time + ahead, miss_after(relative, seconds)};
    }
  }
  return approach;
}

}  // namespace ics::handoff
