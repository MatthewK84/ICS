// slew.h: the axis rates that take a mount to a new target, and how long
// that takes (ICS-039), as python/ics_models/handoff/slew.py gives them.
//
// The tracker's own demand closes an error at its gain, which is too slow
// across the sky: 15 degrees takes 3 s to reach its field of view. A slew
// closes the error as fast as the mount can stop in. Far from the target, the
// closing rate is the one a steady deceleration brings to rest on it, the
// square root of twice the deceleration times the error left; near it, where
// that would chatter, it is a gain times the error. The two meet, with the
// same slope, where the error is the deceleration over the gain squared.
//
// As in the tracker's demand, the error is the target's angle less the
// mount's, both at the time the demand takes effect, the shorter way round in
// azimuth. The demand is the target's rate plus the closing rate, held within
// the axis's rate limit.
#ifndef ICS_HANDOFF_SLEW_H
#define ICS_HANDOFF_SLEW_H

// JSF deviation D-6: ics::encoder::Direction comes from pointing_model.hpp.
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/tracker/demand.h>
#include <ics/tracker/estimator.h>

namespace ics::handoff
{

// What a slew may ask of an axis.
struct Slew_limits
{
  // The fastest an axis turns, in rad/s.
  float64 rate_rad_s;
  // The deceleration the approach is planned on, in rad/s^2, which must be
  // within what the mount can do.
  float64 acceleration_rad_s2;
  // The gain that takes over near the target, a second.
  float64 gain;
};

// Whether the limits are all finite and positive.
[[nodiscard]] bool valid(const Slew_limits& limits);

// The rate that closes an error, towards it: the gain times it when small, a
// steady deceleration's when not. The limits must be valid().
[[nodiscard]] float64 closing_rate(const float64 error, const Slew_limits& limits);

// The rates for a target in axis angles at the time the demand takes effect,
// from the mount's axis angles as last seen.
[[nodiscard]] tracker::Axis_rates slew_demand(const tracker::Angle_state& target,
                                              const tracker::Angle_state& mount,
                                              const Slew_limits& limits);

// How long an axis takes over an angle, from rest to rest, at the limits'
// rate and acceleration, in seconds. The limits must be valid().
[[nodiscard]] float64 slew_seconds(const float64 angle, const Slew_limits& limits);

// How long a mount takes from one direction to another: its slower axis's time.
[[nodiscard]] float64 reach_seconds(const encoder::Direction& first,
                                    const encoder::Direction& second,
                                    const Slew_limits& limits);

}  // namespace ics::handoff

#endif  // ICS_HANDOFF_SLEW_H
