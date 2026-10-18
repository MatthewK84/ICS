// extrapolator.cpp: the target's records into the filter, and the station's cue out (ICS-037).
// JSF deviation D-6: ics::fail, ics::Degrees, ics::Meters and ics::frames come from .hpp headers.
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/cue/extrapolator.h>
#include <ics/cue/filter.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/measurement.h>
#include <ics/cue/pointing.h>
#include <ics/frames/enu.hpp>
#include <ics/frames/geodetic.hpp>
#include <ics/v1/pli.pb.h>

namespace
{

// A configured site as a geodetic point.
ics::Result<ics::frames::Geodetic> point_of(const ics::cue::Site& site)
{
  return ics::frames::Geodetic::make(ics::Degrees(site.latitude_deg),
                                     ics::Degrees(site.longitude_deg),
                                     ics::Meters(site.height_m));
}

// The take of a report the filter was given.
ics::cue::Take take_of(const ics::cue::Outcome outcome)
{
  ics::cue::Take take = ics::cue::Take::too_old;
  if (outcome == ics::cue::Outcome::taken)
  {
    take = ics::cue::Take::taken;
  }
  else if (outcome == ics::cue::Outcome::late)
  {
    take = ics::cue::Take::late;
  }
  else if (outcome == ics::cue::Outcome::gated)
  {
    take = ics::cue::Take::gated;
  }
  else
  {
    take = ics::cue::Take::too_old;
  }
  return take;
}

// Counts one take.
void count(const ics::cue::Take take, ics::cue::Counts& counts)
{
  switch (take)
  {
    case ics::cue::Take::taken:
      ++counts.taken;
      break;
    case ics::cue::Take::late:
      ++counts.late;
      break;
    case ics::cue::Take::gated:
      ++counts.gated;
      break;
    case ics::cue::Take::too_old:
      ++counts.too_old;
      break;
    case ics::cue::Take::not_target:
      ++counts.not_target;
      break;
    default:
      ++counts.unusable;
      break;
  }
}

}  // namespace

namespace ics::cue
{

Result<Cue_extrapolator> Cue_extrapolator::make(const Cue_settings& settings)
{
  Result<Cue_extrapolator> made = fail(Error::kInvalidArgument);
  const Result<frames::Geodetic> origin = point_of(settings.range_origin);
  const Result<frames::Geodetic> site = point_of(settings.station);
  if (!origin.has_value())
  {
    made = fail(origin.error());
  }
  else if (!site.has_value())
  {
    made = fail(site.error());
  }
  else
  {
    const frames::EnuFrame range(*origin);
    made = Cue_extrapolator(settings, range, station_at(range, *site));
  }
  return made;
}

Cue_extrapolator::Cue_extrapolator(const Cue_settings& settings,
                                   const frames::EnuFrame& range,
                                   const Station& station)
  : settings_(settings), range_(range), station_(station), filter_(settings.filter), counts_()
{
}

Take Cue_extrapolator::take(const v1::PliRecord& record)
{
  Take take = Take::not_target;
  if (record.role() == settings_.target_role)
  {
    const Result<Measurement> report = measurement_from(record, range_, settings_.defaults);
    take = report.has_value() ? take_of(filter_.add(*report)) : Take::unusable;
  }
  count(take, counts_);
  return take;
}

Result<Cue> Cue_extrapolator::cue_at(const UtcTime now) const
{
  Result<Cue> cue = fail(Error::kEmpty);
  const Result<UtcTime> latest = filter_.latest();
  if (latest.has_value())
  {
    const Result<Estimate> estimate = filter_.estimate_at(now + settings_.command_latency);
    if (estimate.has_value())
    {
      cue = cue_for(*estimate, station_);
    }
    else
    {
      cue = fail(estimate.error());
    }
  }
  return cue;
}

Counts Cue_extrapolator::counts() const
{
  return counts_;
}

}  // namespace ics::cue
