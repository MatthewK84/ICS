// extrapolator.h: the cue extrapolator for one target and one station (ICS-037, CPP-18).
// It takes every PLI record a station hears, keeps those of the target's role
// (ENTITY_ROLE_TARGET, as each adapter assigns it), fuses them in a Cue_filter,
// and gives the station's cue at the moment a mount command sent now takes
// effect: now plus the mount's command latency. The newest report's age is
// covered too, since the filter predicts from it. There is no data association:
// the operator marks which reports are the target.
#ifndef ICS_CUE_EXTRAPOLATOR_H
#define ICS_CUE_EXTRAPOLATOR_H

// JSF deviation D-6: ics::Result, ics::Duration, ics::UtcTime and ics::frames come from .hpp headers.
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/filter.h>
#include <ics/cue/measurement.h>
#include <ics/cue/pointing.h>
#include <ics/frames/enu.hpp>
#include <ics/v1/common.pb.h>
#include <ics/v1/pli.pb.h>

namespace ics::cue
{

// A WGS 84 point as the config gives it.
struct Site
{
  // Degrees north.
  float64 latitude_deg;
  // Degrees east.
  float64 longitude_deg;
  // Metres above the ellipsoid.
  float64 height_m;
};

// Everything the extrapolator needs: where the range and the station are,
// which reports are the target, and how the filter weighs them.
struct Cue_settings
{
  // The range ENU frame's origin, as every other ICS module takes it.
  Site range_origin;
  // The station's mount.
  Site station;
  // The role of the target's reports.
  v1::EntityRole target_role;
  // From sending a mount command to its taking effect.
  Duration command_latency;
  // The filter's settings.
  Filter_settings filter;
  // The sigmas of a record that carries none.
  Noise_defaults defaults;
};

// What became of a record.
enum class Take
{
  taken,
  late,
  gated,
  too_old,
  not_target,
  unusable
};

// How many records met each fate.
struct Counts
{
  // Applied in time order.
  int64 taken;
  // Applied out of time order, by re-filtering.
  int64 late;
  // Too far from the prediction.
  int64 gated;
  // Older than the history.
  int64 too_old;
  // Of another role.
  int64 not_target;
  // With no fix or a bad value.
  int64 unusable;
};

// The extrapolator for one target and one station.
class Cue_extrapolator
{
public:
  // The extrapolator for settings. Fails as frames::Geodetic::make does for
  // the range origin or the station, with kInvalidArgument or kOutOfRange.
  [[nodiscard]] static Result<Cue_extrapolator> make(const Cue_settings& settings);

  // Takes a record the station heard.
  [[nodiscard]] Take take(const v1::PliRecord& record);

  // The station's cue for a mount command sent at now. Fails as the filter's
  // estimate_at and cue_for do, and with kEmpty before the first report.
  [[nodiscard]] Result<Cue> cue_at(const UtcTime now) const;

  // The counts so far.
  [[nodiscard]] Counts counts() const;

private:
  Cue_extrapolator(const Cue_settings& settings,
                   const frames::EnuFrame& range,
                   const Station& station);

  // The settings.
  Cue_settings settings_;
  // The range ENU frame.
  frames::EnuFrame range_;
  // The station, in the range frame.
  Station station_;
  // The filter.
  Cue_filter filter_;
  // The counts so far.
  Counts counts_;
};

}  // namespace ics::cue

#endif  // ICS_CUE_EXTRAPOLATOR_H
