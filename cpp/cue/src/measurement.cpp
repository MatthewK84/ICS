// measurement.cpp: an ics.v1.PliRecord as a report in the range ENU frame (ICS-037).
// JSF deviation D-6: ics::fail, ics::Degrees, ics::Meters and ics::frames come from .hpp headers.
#include <chrono>
#include <cmath>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/measurement.h>
#include <ics/frames/enu.hpp>
#include <ics/frames/geodetic.hpp>
#include <ics/v1/common.pb.h>
#include <ics/v1/pli.pb.h>

namespace
{

// The unit of a velocity, as a tag for frames' vectors.
struct Meter_per_second;

using Velocity = ics::frames::EnuVector<Meter_per_second>;

// Whether a sigma can weigh a report: positive and finite.
bool usable_sigma(const ics::float64 sigma)
{
  return std::isfinite(sigma) && (sigma > 0.0);
}

// The record's position as a geodetic point.
ics::Result<ics::frames::Geodetic> point_of(const ics::v1::PliRecord& record)
{
  const ics::v1::GeodeticPoint& position = record.position();
  return ics::frames::Geodetic::make(ics::Degrees(position.latitude_deg()),
                                     ics::Degrees(position.longitude_deg()),
                                     ics::Meters(position.height_ellipsoid_m()));
}

// The record's velocity, in the ENU axes at the vehicle, turned into the range frame's.
ics::cue::Vector3 velocity_in_range(const ics::v1::EnuVector& velocity,
                                    const ics::frames::Geodetic& point,
                                    const ics::frames::EnuFrame& range)
{
  const ics::frames::EnuFrame here(point);
  const Velocity local{ics::Quantity<Meter_per_second>(velocity.east()),
                       ics::Quantity<Meter_per_second>(velocity.north()),
                       ics::Quantity<Meter_per_second>(velocity.up())};
  const Velocity turned = range.rotate(here.rotate(local));
  return {turned.east.value(), turned.north.value(), turned.up.value()};
}

// The report at a point, before its values are checked.
ics::cue::Measurement report_at(const ics::v1::PliRecord& record,
                                const ics::frames::Geodetic& point,
                                const ics::frames::EnuFrame& range,
                                const ics::cue::Noise_defaults& defaults)
{
  const ics::cue::Fix_sigmas fallback = ics::cue::defaults_for(record.fix_type(), defaults);
  const ics::frames::Enu position = range.to_enu(point);
  ics::cue::Measurement report{};
  report.time = ics::UtcTime{} + std::chrono::nanoseconds(record.valid_utc_ns());
  report.position = {position.east.value(), position.north.value(), position.up.value()};
  report.horizontal_sigma_m = record.has_horizontal_sigma_m() ? record.horizontal_sigma_m() : fallback.horizontal_m;
  report.vertical_sigma_m = record.has_vertical_sigma_m() ? record.vertical_sigma_m() : fallback.vertical_m;
  report.has_velocity = record.has_velocity_enu_mps();
  report.velocity_sigma_m_s = defaults.velocity_sigma_m_s;
  if (report.has_velocity)
  {
    report.velocity = velocity_in_range(record.velocity_enu_mps(), point, range);
  }
  return report;
}

// Whether a report's time, velocity and sigmas can be used.
bool well_formed(const ics::cue::Measurement& report)
{
  bool moving = true;
  for (const ics::float64 component : report.velocity)
  {
    moving = moving && std::isfinite(component);
  }
  const bool sigmas = usable_sigma(report.horizontal_sigma_m) && usable_sigma(report.vertical_sigma_m);
  const bool timed = report.time.time_since_epoch().count() != 0;
  return timed && moving && sigmas && usable_sigma(report.velocity_sigma_m_s);
}

}  // namespace

namespace ics::cue
{

Fix_sigmas defaults_for(const v1::PliRecord::FixType fix_type, const Noise_defaults& defaults)
{
  Fix_sigmas sigmas = defaults.other;
  switch (fix_type)
  {
    case v1::PliRecord::FIX_TYPE_TWO_DIMENSIONAL:
      sigmas = defaults.two_dimensional;
      break;
    case v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL:
      sigmas = defaults.three_dimensional;
      break;
    case v1::PliRecord::FIX_TYPE_DGNSS:
      sigmas = defaults.dgnss;
      break;
    case v1::PliRecord::FIX_TYPE_RTK_FLOAT:
      sigmas = defaults.rtk_float;
      break;
    case v1::PliRecord::FIX_TYPE_RTK_FIXED:
      sigmas = defaults.rtk_fixed;
      break;
    default:
      break;
  }
  return sigmas;
}

Result<Measurement> measurement_from(const v1::PliRecord& record,
                                     const frames::EnuFrame& range,
                                     const Noise_defaults& defaults)
{
  Result<Measurement> report = fail(Error::kUnconstrained);
  if (record.fix_type() != v1::PliRecord::FIX_TYPE_NONE)
  {
    const Result<frames::Geodetic> point = point_of(record);
    if (!point.has_value())
    {
      report = fail(point.error());
    }
    else
    {
      const Measurement made = report_at(record, *point, range, defaults);
      if (well_formed(made))
      {
        report = made;
      }
      else
      {
        report = fail(Error::kMalformed);
      }
    }
  }
  return report;
}

}  // namespace ics::cue
