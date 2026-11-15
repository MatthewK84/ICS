// approach.h: when an interceptor is closest to its target (ICS-039), as
// python/ics_models/handoff/approach.py gives it. Both vehicles are taken to
// keep the velocity they have now, so their separation is closest where it
// stops closing: after (p . v) / (v . v) seconds, for the interceptor's
// position p from the target and the closing velocity v, the target's less
// the interceptor's. This is coarse, and it is for putting engagements in
// order: the trigger (ICS-040) predicts the moment itself.
//
// There is no approach to predict while the interceptor is slower than the
// launch speed, for it has not launched; while the two are not closing; or
// further ahead than the horizon, where the velocities will not last.
#ifndef ICS_HANDOFF_APPROACH_H
#define ICS_HANDOFF_APPROACH_H

// JSF deviation D-6: ics::Duration and ics::UtcTime come from units.hpp.
#include <optional>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>

namespace ics::handoff
{

// When there is an approach to predict.
struct Approach_settings
{
  // An interceptor slower than this has not launched, in m/s.
  float64 launch_speed_m_s;
  // An approach further ahead than this is not predicted.
  Duration horizon;
};

// Whether the launch speed is finite and positive, and the horizon positive.
[[nodiscard]] bool valid(const Approach_settings& settings);

// A closest approach.
struct Approach
{
  // When the two are closest.
  UtcTime time;
  // How far apart they are then, in metres.
  float64 miss_m;
};

// The closest approach ahead of a target and an interceptor, or nothing when
// there is none to predict. The estimates must be at one time: for two
// times, a check fails and there is nothing.
[[nodiscard]] std::optional<Approach> closest_approach(const cue::Estimate& target,
                                                       const cue::Estimate& interceptor,
                                                       const Approach_settings& settings);

}  // namespace ics::handoff

#endif  // ICS_HANDOFF_APPROACH_H
