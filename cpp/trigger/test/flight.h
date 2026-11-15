// flight.h: scripted flights and their PLI for the trigger's tests (ICS-040),
// as the hand-off scheduler's tests fly them (ICS-039). Vehicles fly straight
// lines in the range frame. Their PLI comes as the MAVLink adapter gives it:
// in GLOBAL_POSITION_INT's 1e-7 degree and millimetre steps, with GPS_RAW_INT's
// accuracies.
#ifndef ICS_TRIGGER_TEST_FLIGHT_H
#define ICS_TRIGGER_TEST_FLIGHT_H

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/extrapolator.h>
#include <ics/cue/filter.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/measurement.h>
#include <ics/frames/enu.hpp>
#include <ics/frames/geodetic.hpp>
#include <ics/v1/common.pb.h>
#include <ics/v1/pli.pb.h>

namespace ics::trigger_test
{

using cue::Vector3;

inline constexpr int64 start_ns = 1760000000000000000;
inline constexpr float64 per_second = 1.0E9;
inline constexpr float64 degree_step = 1.0E7;
inline constexpr float64 millimetres = 1000.0;
inline constexpr float64 centimetres = 100.0;
inline constexpr float64 pli_horizontal_sigma_m = 0.5;
inline constexpr float64 pli_vertical_sigma_m = 0.8;
inline constexpr cue::Fix_sigmas fallback{1.5, 3.0};
inline constexpr cue::Filter_settings filter{
  {4.0, 1.0}, 16.27, std::chrono::seconds(2), std::chrono::seconds(2), cue::max_history, 20.0, 10.0};
inline constexpr cue::Noise_defaults defaults{fallback, fallback, fallback, fallback, fallback, fallback, 0.2};
inline constexpr cue::Site origin{40.0, -100.0, 700.0};

// The cue settings the trigger takes: the range, the filter and the sigmas.
inline cue::Cue_settings cue_settings()
{
  return {origin, origin, v1::ENTITY_ROLE_TARGET, Duration::zero(), filter, defaults};
}

// A time since the start.
inline UtcTime at(const int64 since_ns)
{
  return UtcTime{} + Duration(start_ns + since_ns);
}

// The truth at one instant, in the range ENU frame: metres, and metres a second.
struct Sample
{
  Vector3 position;
  Vector3 velocity;
};

// A straight flight from one point to another. With a ramp, the vehicle is
// parked until it leaves and gathers its speed over the ramp; with none, it
// flies at one speed before and after. Either way it arrives on time, and
// flies on.
struct Flight
{
  Vector3 from;
  Vector3 to;
  float64 leaves_s;
  float64 arrives_s;
  float64 ramp_s;
};

// How far along its line a flight is, and how fast, a time after it leaves.
inline std::array<float64, 2> progress_of(const Flight& flight,
                                          const float64 cruise,
                                          const float64 since)
{
  std::array<float64, 2> progress{cruise * since, cruise};
  if (flight.ramp_s > 0.0)
  {
    if (since <= 0.0)
    {
      progress = {0.0, 0.0};
    }
    else if (since < flight.ramp_s)
    {
      const float64 phase = (std::numbers::pi * since) / flight.ramp_s;
      const float64 lag = (flight.ramp_s / (2.0 * std::numbers::pi)) * std::sin(phase);
      progress = {cruise * ((since / 2.0) - lag), (cruise * (1.0 - std::cos(phase))) / 2.0};
    }
    else
    {
      progress = {cruise * (since - (flight.ramp_s / 2.0)), cruise};
    }
  }
  return progress;
}

// Where a flight is, and how it moves, at a time since the start.
inline Sample sample_of(const Flight& flight, const float64 seconds)
{
  const Vector3 line{flight.to[0] - flight.from[0], flight.to[1] - flight.from[1], flight.to[2] - flight.from[2]};
  const float64 length = std::hypot(line[0], line[1], line[2]);
  const float64 cruise = length / ((flight.arrives_s - flight.leaves_s) - (flight.ramp_s / 2.0));
  const std::array<float64, 2> progress = progress_of(flight, cruise, seconds - flight.leaves_s);
  Sample sample{};
  for (int32 axis = 0; axis < cue::axes; ++axis)
  {
    const std::size_t place = static_cast<std::size_t>(axis);
    const float64 unit = line[place] / length;
    sample.position[place] = flight.from[place] + (unit * progress[0]);
    sample.velocity[place] = unit * progress[1];
  }
  return sample;
}

inline frames::EnuFrame range_frame()
{
  const Result<frames::Geodetic> made =
    frames::Geodetic::make(Degrees(origin.latitude_deg), Degrees(origin.longitude_deg), Meters(origin.height_m));
  EXPECT_TRUE(made.has_value());
  return frames::EnuFrame(*made);
}

// A record of a vehicle, as the MAVLink adapter gives it.
inline v1::PliRecord record_of(const frames::EnuFrame& range,
                               const std::string& name,
                               const Sample& sample,
                               const int64 valid_ns)
{
  const Result<frames::Geodetic> found = range.to_geodetic(
    {Meters(sample.position[0]), Meters(sample.position[1]), Meters(sample.position[2])});
  EXPECT_TRUE(found.has_value());
  v1::PliRecord made;
  made.set_entity_id(name);
  made.set_valid_utc_ns(valid_ns);
  made.set_received_utc_ns(valid_ns);
  made.set_source(v1::PLI_SOURCE_MAVLINK);
  made.set_fix_type(v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  made.set_horizontal_sigma_m(pli_horizontal_sigma_m);
  made.set_vertical_sigma_m(pli_vertical_sigma_m);
  made.mutable_position()->set_latitude_deg(std::round(found->latitude().value() * degree_step) / degree_step);
  made.mutable_position()->set_longitude_deg(std::round(found->longitude().value() * degree_step) / degree_step);
  made.mutable_position()->set_height_ellipsoid_m(std::round(found->height().value() * millimetres) / millimetres);
  const frames::EnuFrame here(*found);
  const frames::Enu axes{Meters(sample.velocity[0]), Meters(sample.velocity[1]), Meters(sample.velocity[2])};
  const frames::Enu local = here.rotate(range.rotate(axes));
  made.mutable_velocity_enu_mps()->set_east(std::round(local.east.value() * centimetres) / centimetres);
  made.mutable_velocity_enu_mps()->set_north(std::round(local.north.value() * centimetres) / centimetres);
  made.mutable_velocity_enu_mps()->set_up(std::round(local.up.value() * centimetres) / centimetres);
  return made;
}

}  // namespace ics::trigger_test

#endif  // ICS_TRIGGER_TEST_FLIGHT_H
