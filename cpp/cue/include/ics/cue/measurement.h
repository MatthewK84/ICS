// measurement.h: a target report in the range ENU frame, from an ics.v1.PliRecord (ICS-037).
// A report is a position with one-sigma horizontal and vertical errors, and a
// velocity when the source gives one: GNSS PLI over MAVLink, CoT or Lattice
// does, SAPIENT radar does not. A record that carries no sigmas takes its fix
// type's defaults from the config.
#ifndef ICS_CUE_MEASUREMENT_H
#define ICS_CUE_MEASUREMENT_H

// JSF deviation D-6: ics::Result, ics::UtcTime and ics::frames::EnuFrame come from .hpp headers.
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>
#include <ics/frames/enu.hpp>
#include <ics/v1/pli.pb.h>

namespace ics::cue
{

// A position report, in the range ENU frame.
struct Measurement
{
  // When the position held, on UTC.
  UtcTime time;
  // East, north and up, in metres.
  Vector3 position;
  // One-sigma error of east and of north, in metres.
  float64 horizontal_sigma_m;
  // One-sigma error of up, in metres.
  float64 vertical_sigma_m;
  // Whether the report has a velocity.
  bool has_velocity;
  // East, north and up velocity in the range frame's axes, in m/s; zero without one.
  Vector3 velocity;
  // One-sigma error of each velocity component, in m/s.
  float64 velocity_sigma_m_s;
};

// One-sigma position errors.
struct Fix_sigmas
{
  // East and north, in metres.
  float64 horizontal_m;
  // Up, in metres.
  float64 vertical_m;
};

// The errors given to a record that carries none, by its fix type.
struct Noise_defaults
{
  // A two-dimensional GNSS fix, whose height is poor.
  Fix_sigmas two_dimensional;
  // A three-dimensional GNSS fix.
  Fix_sigmas three_dimensional;
  // Differential GNSS.
  Fix_sigmas dgnss;
  // RTK with a float solution.
  Fix_sigmas rtk_float;
  // RTK with a fixed solution.
  Fix_sigmas rtk_fixed;
  // Anything else, such as a radar position with an unspecified fix.
  Fix_sigmas other;
  // Every velocity, which no record gives a sigma for, in m/s.
  float64 velocity_sigma_m_s;
};

// The sigmas a record of a fix type takes when it carries none.
[[nodiscard]] Fix_sigmas defaults_for(const v1::PliRecord::FixType fix_type, const Noise_defaults& defaults);

// The report a record gives, in the range ENU frame. Fails with
// kUnconstrained for a record with no fix; kInvalidArgument for a position
// that is not finite, and kOutOfRange for one past 90 degrees of latitude, as
// frames::Geodetic::make does; and kMalformed for a time of 0, a velocity that
// is not finite or a sigma that is not positive and finite.
[[nodiscard]] Result<Measurement> measurement_from(const v1::PliRecord& record,
                                                   const frames::EnuFrame& range,
                                                   const Noise_defaults& defaults);

}  // namespace ics::cue

#endif  // ICS_CUE_MEASUREMENT_H
