// approach.h: when an interceptor is closest to its target, to fire the
// cameras by (ICS-040), as python/ics_models/trigger/approach.py gives it.
//
// coarse_approach takes both vehicles to keep the velocity they have now, as
// the hand-off scheduler's does (ICS-039): it is repeated here so that the
// trigger does not link the scheduler, its tracker and the tracking camera's
// driver. refined_approach starts there and takes them to keep their
// acceleration too. The separation is s(t) = p - c t - g t^2 / 2, for the
// interceptor's position p from the target, the closing velocity c and the
// closing acceleration g, each the target's less the interceptor's. It is
// closest where s(t) . s'(t) = 0, found by a fixed number of Newton steps
// from the constant-velocity answer.
//
// The steps are kept only if every one of them had the separation curving
// up, and they end ahead of now and inside the horizon. Otherwise the
// constant-velocity answer stands.
#ifndef ICS_TRIGGER_APPROACH_H
#define ICS_TRIGGER_APPROACH_H

// JSF deviation D-6: ics::Duration and ics::UtcTime come from units.hpp.
#include <optional>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>

namespace ics::trigger
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

// The closest approach ahead of a target and an interceptor that keep their
// velocity, or nothing when there is none to predict: the interceptor has
// not launched, the two are not closing, or it is past the horizon. The
// estimates must be at one time: for two times, a check fails and there is
// nothing.
[[nodiscard]] std::optional<Approach> coarse_approach(const cue::Estimate& target,
                                                      const cue::Estimate& interceptor,
                                                      const Approach_settings& settings);

// The closest approach ahead of a target and an interceptor that keep their
// acceleration, or nothing when coarse_approach has none.
[[nodiscard]] std::optional<Approach> refined_approach(const cue::Estimate& target,
                                                       const cue::Estimate& interceptor,
                                                       const Approach_settings& settings);

}  // namespace ics::trigger

#endif  // ICS_TRIGGER_APPROACH_H
