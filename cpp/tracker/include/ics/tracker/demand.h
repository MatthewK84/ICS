// demand.h: the axis rates that follow the target and close the pointing
// error (ICS-038), as python/ics_models/tracker/demand.py gives them. A
// demand takes effect a lead time after it is made. By then the mount has
// moved on at its own rates, and the target is where its state predicts. The
// demand is the target's rate plus gain times the remaining error, in the
// mount's own axis angles (the shorter way round in azimuth). The target's
// rates on the sky stand in for its axis rates: the pointing model turns
// them by well under a thousandth.
#ifndef ICS_TRACKER_DEMAND_H
#define ICS_TRACKER_DEMAND_H

#include <ics/common/universal_types.h>
#include <ics/tracker/estimator.h>

namespace ics::tracker
{

// Axis rates, in radians a second.
struct Axis_rates
{
  float64 azimuth_rad_s;
  float64 elevation_rad_s;
};

// The rates for a target in axis angles at the time the demand takes
// effect, from the mount's axis angles as last seen. The gain, in
// reciprocal seconds, must not be negative; a negative gain fails a check
// and steers with none.
[[nodiscard]] Axis_rates demand(const Angle_state& target,
                                const Angle_state& mount,
                                const float64 gain);

}  // namespace ics::tracker

#endif  // ICS_TRACKER_DEMAND_H
