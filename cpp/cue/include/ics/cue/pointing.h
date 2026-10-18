// pointing.h: a station's cue from an estimate (ICS-037), as
// python/ics_models/cue/pointing.py gives it. The cue is in the station's own
// ENU axes, which differ from the range frame's by the turn of the ellipsoid
// normal between them, about 50 microradians at 300 m. Azimuth is clockwise
// from north in [0, 2 pi); elevation is above the station's horizontal. The
// rates come from the estimated velocity, with the station fixed. The pointing
// uncertainty is the one-sigma angle of the position covariance across the
// line of sight, along its larger axis.
#ifndef ICS_CUE_POINTING_H
#define ICS_CUE_POINTING_H

// JSF deviation D-6: ics::Result, ics::UtcTime and ics::frames come from .hpp headers.
#include <array>
#include <cstddef>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>
#include <ics/frames/enu.hpp>
#include <ics/frames/geodetic.hpp>

namespace ics::cue
{

// A 3 by 3 rotation, row by row.
using Rotation = std::array<float64, axes_length * axes_length>;

// A station: its position in the range ENU frame, and the rotation from the range frame's axes into its own.
struct Station
{
  // East, north and up from the range origin, in metres.
  Vector3 position;
  // Range ENU axes to the station's ENU axes.
  Rotation axes;
};

// Where a station points at a time.
struct Cue
{
  // The time the cue holds for.
  UtcTime time;
  // Clockwise from north, in [0, 2 pi).
  float64 azimuth_rad;
  // Above the station's horizontal.
  float64 elevation_rad;
  // The azimuth's rate.
  float64 azimuth_rate_rad_s;
  // The elevation's rate.
  float64 elevation_rate_rad_s;
  // The distance to the target, in metres.
  float64 range_m;
  // The one-sigma pointing uncertainty.
  float64 sigma_rad;
};

// The station at a site, for the range ENU frame.
[[nodiscard]] Station station_at(const frames::EnuFrame& range, const frames::Geodetic& site);

// An angle from atan2, in [-pi, pi], as an azimuth in [0, 2 pi). A tiny
// negative angle plus 2 pi rounds to 2 pi itself, which is due north: 0.
[[nodiscard]] float64 wrap_azimuth(const float64 angle);

// The station's cue for an estimate. Fails with kUnconstrained when the target
// is within 1 cm of the station's vertical or 1 m of the station, where its
// direction is not defined well enough to cue.
[[nodiscard]] Result<Cue> cue_for(const Estimate& estimate, const Station& station);

}  // namespace ics::cue

#endif  // ICS_CUE_POINTING_H
