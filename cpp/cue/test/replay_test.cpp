// replay_test.cpp: ICS-037's "Done when" test: pointing error under 2 mrad on replayed tracks.
// No SITL record of the crossing is in the repository, so the target flies
// deploy/sitl/engagements/crossing.toml's path synthetically: climb-out to
// 35 m, acceleration to 5 m/s east and the crossing. Its truth is a full-rate
// table, as PX4's onboard log would be. The PLI is that table at the SITL
// fixtures' 10 Hz, as the MAVLink adapter gives it: GLOBAL_POSITION_INT's
// 1e-7 degree and millimetre steps, received 100 ms late, with GPS_RAW_INT's
// accuracies. The radar is synthetic, as question 3 sets it: 2 Hz, 3 m
// one-sigma per axis, 300 ms latency, with dropouts. A second, harder track
// manoeuvres at 15 m/s with turns of 1 g. Three stations 250 to 300 m from the
// crossing point each take a cue every 20 ms, for a command sent then; from
// 3 s on, each cue's direction is compared with the truth at the moment the
// command takes effect.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/extrapolator.h>
#include <ics/cue/filter.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/measurement.h>
#include <ics/cue/pointing.h>
#include <ics/frames/enu.hpp>
#include <ics/frames/geodetic.hpp>
#include <ics/v1/common.pb.h>
#include <ics/v1/pli.pb.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;
using ics::cue::Vector3;
using ics::v1::PliRecord;

constexpr int64 start_ns = 1760000000000000000;
constexpr int64 step_ns = 1000000;
constexpr int64 pli_period_ns = 100000000;
constexpr int64 pli_latency_ns = 100000000;
constexpr int64 radar_period_ns = 500000000;
constexpr int64 radar_latency_ns = 300000000;
constexpr int64 tick_ns = 20000000;
constexpr int64 settle_ns = 3000000000;
constexpr int64 latency_ns = 50000000;
constexpr float64 step_s = 0.001;
constexpr float64 radar_sigma_m = 3.0;
constexpr float64 radar_dropout = 0.2;
constexpr float64 pli_horizontal_sigma_m = 0.5;
constexpr float64 pli_vertical_sigma_m = 0.8;
constexpr float64 degree_step = 1.0E7;
constexpr float64 millimetres = 1000.0;
constexpr float64 centimetres = 100.0;
constexpr float64 budget_rad = 0.002;
constexpr float64 gravity = 9.80665;
constexpr int64 seed = 37;
constexpr float64 per_milliradian = 1000.0;
constexpr float64 crossing_s = 90.0;
constexpr float64 manoeuvring_s = 30.0;
// Where each track starts, in the range frame.
constexpr Vector3 crossing_start{-200.0, 0.0, 5.0};
constexpr Vector3 manoeuvring_start{-150.0, -150.0, 35.0};
// The manoeuvring track starts heading north-east.
constexpr float64 north_east = std::numbers::pi / 4.0;
constexpr ics::cue::Fix_sigmas fallback{1.5, 3.0};
constexpr ics::cue::Filter_settings filter{
  {4.0, 1.0}, 16.27, std::chrono::seconds(2), std::chrono::seconds(2), ics::cue::max_history, 20.0, 10.0};
constexpr ics::cue::Noise_defaults defaults{fallback, fallback, fallback, fallback, fallback, fallback, 0.2};
constexpr std::array<ics::cue::Site, 3> stations{
  {{39.9977477, -100.0, 702.0}, {40.0010812, -99.9969537, 702.0}, {40.0018919, -100.0021096, 702.0}}};

// The truth at one instant, in the range ENU frame.
struct Sample
{
  // Metres.
  Vector3 position;
  // Metres per second.
  Vector3 velocity;
};

// A track: the truth every millisecond from start_ns.
using Track = std::vector<Sample>;

// The crossing target's velocity: a 10 s climb from 5 m to 35 m, then 3 s of
// acceleration to 5 m/s east.
Vector3 crossing_velocity(const float64 seconds)
{
  const float64 climb_s = 10.0;
  const float64 climb_m = 30.0;
  const float64 speed = 5.0;
  const float64 accelerate_s = 3.0;
  Vector3 velocity{0.0, 0.0, 0.0};
  if (seconds < climb_s)
  {
    velocity[2] = (climb_m / climb_s) * (1.0 - std::cos((2.0 * std::numbers::pi * seconds) / climb_s));
  }
  else if (seconds < (climb_s + accelerate_s))
  {
    velocity[0] = (speed / 2.0) * (1.0 - std::cos((std::numbers::pi * (seconds - climb_s)) / accelerate_s));
  }
  else
  {
    velocity[0] = speed;
  }
  return velocity;
}

// The manoeuvring target's turn rate: 3 s straight, then 4 s turning at 1 g,
// alternately left and right, each turn rolling in and out over 0.5 s.
float64 turn_rate(const float64 seconds, const float64 speed)
{
  const float64 straight_s = 3.0;
  const float64 turn_s = 4.0;
  const float64 roll_s = 0.5;
  const float64 cycle_s = straight_s + turn_s;
  const float64 into = std::fmod(seconds, cycle_s) - straight_s;
  const bool left = std::fmod(std::floor(seconds / cycle_s), 2.0) < 1.0;
  float64 rate = 0.0;
  if (into > 0.0)
  {
    const float64 edge = std::fmin(into, turn_s - into);
    const float64 roll = (edge >= roll_s) ? 1.0 : ((1.0 - std::cos((std::numbers::pi * edge) / roll_s)) / 2.0);
    rate = ((left ? -1.0 : 1.0) * roll * gravity) / speed;
  }
  return rate;
}

// The truth table of a track, by integrating its velocity with the trapezoid rule.
Track crossing_track(const float64 duration_s)
{
  Track track;
  Sample now{crossing_start, crossing_velocity(0.0)};
  const int64 steps = static_cast<int64>(duration_s / step_s);
  for (int64 number = 0; number <= steps; ++number)
  {
    track.push_back(now);
    const Vector3 next = crossing_velocity(static_cast<float64>(number + 1) * step_s);
    for (int32 axis = 0; axis < ics::cue::axes; ++axis)
    {
      const std::size_t place = static_cast<std::size_t>(axis);
      now.position[place] += ((now.velocity[place] + next[place]) / 2.0) * step_s;
    }
    now.velocity = next;
  }
  return track;
}

// The manoeuvring target: 15 m/s at 35 m, from south-west of the crossing point, heading north-east.
Track manoeuvring_track(const float64 duration_s)
{
  const float64 speed = 15.0;
  float64 heading = north_east;
  Track track;
  Sample now{manoeuvring_start, {speed * std::sin(heading), speed * std::cos(heading), 0.0}};
  const int64 steps = static_cast<int64>(duration_s / step_s);
  for (int64 number = 0; number <= steps; ++number)
  {
    track.push_back(now);
    heading += turn_rate((static_cast<float64>(number) + 0.5) * step_s, speed) * step_s;
    const Vector3 next{speed * std::sin(heading), speed * std::cos(heading), 0.0};
    now.position[0] += ((now.velocity[0] + next[0]) / 2.0) * step_s;
    now.position[1] += ((now.velocity[1] + next[1]) / 2.0) * step_s;
    now.velocity = next;
  }
  return track;
}

ics::frames::Geodetic point(const ics::cue::Site& site)
{
  const ics::Result<ics::frames::Geodetic> made = ics::frames::Geodetic::make(
    ics::Degrees(site.latitude_deg), ics::Degrees(site.longitude_deg), ics::Meters(site.height_m));
  EXPECT_TRUE(made.has_value());
  return *made;
}

const ics::cue::Site origin{40.0, -100.0, 700.0};

const Sample& truth_at(const Track& track, const int64 since_start_ns)
{
  return track[static_cast<std::size_t>(since_start_ns / step_ns)];
}

ics::frames::Geodetic geodetic_of(const ics::frames::EnuFrame& range, const Vector3& position)
{
  const ics::Result<ics::frames::Geodetic> found =
    range.to_geodetic({ics::Meters(position[0]), ics::Meters(position[1]), ics::Meters(position[2])});
  EXPECT_TRUE(found.has_value());
  return *found;
}

// A record of the target at a time, as an adapter gives it.
PliRecord record_at(const ics::frames::EnuFrame& range,
                    const Vector3& position,
                    const int64 valid_ns,
                    const int64 received_ns)
{
  const ics::frames::Geodetic where = geodetic_of(range, position);
  PliRecord made;
  made.set_role(ics::v1::ENTITY_ROLE_TARGET);
  made.set_valid_utc_ns(valid_ns);
  made.set_received_utc_ns(received_ns);
  made.mutable_position()->set_latitude_deg(std::round(where.latitude().value() * degree_step) / degree_step);
  made.mutable_position()->set_longitude_deg(std::round(where.longitude().value() * degree_step) / degree_step);
  made.mutable_position()->set_height_ellipsoid_m(std::round(where.height().value() * millimetres) / millimetres);
  return made;
}

// The PLI: GLOBAL_POSITION_INT every 100 ms, with its velocity in the ENU axes at the vehicle.
std::vector<PliRecord> pli_records(const Track& track, const ics::frames::EnuFrame& range)
{
  std::vector<PliRecord> records;
  const int64 span_ns = (static_cast<int64>(track.size()) - 1) * step_ns;
  for (int64 since = 0; since <= span_ns; since += pli_period_ns)
  {
    const Sample& now = truth_at(track, since);
    PliRecord made = record_at(range, now.position, start_ns + since, start_ns + since + pli_latency_ns);
    made.set_source(ics::v1::PLI_SOURCE_MAVLINK);
    made.set_fix_type(PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
    made.set_horizontal_sigma_m(pli_horizontal_sigma_m);
    made.set_vertical_sigma_m(pli_vertical_sigma_m);
    const ics::frames::EnuFrame here(geodetic_of(range, now.position));
    const ics::frames::Enu axes{
      ics::Meters(now.velocity[0]), ics::Meters(now.velocity[1]), ics::Meters(now.velocity[2])};
    const ics::frames::Enu local = here.rotate(range.rotate(axes));
    made.mutable_velocity_enu_mps()->set_east(std::round(local.east.value() * centimetres) / centimetres);
    made.mutable_velocity_enu_mps()->set_north(std::round(local.north.value() * centimetres) / centimetres);
    made.mutable_velocity_enu_mps()->set_up(std::round(local.up.value() * centimetres) / centimetres);
    records.push_back(made);
  }
  return records;
}

// The radar: a position every 500 ms with 3 m of noise per axis, received 300 ms late, a fifth of them lost.
std::vector<PliRecord> radar_records(const Track& track, const ics::frames::EnuFrame& range)
{
  std::mt19937_64 generator(seed);
  std::normal_distribution<float64> noise(0.0, radar_sigma_m);
  std::uniform_real_distribution<float64> chance(0.0, 1.0);
  std::vector<PliRecord> records;
  const int64 span_ns = (static_cast<int64>(track.size()) - 1) * step_ns;
  for (int64 since = radar_period_ns / 2; since <= span_ns; since += radar_period_ns)
  {
    Vector3 seen = truth_at(track, since).position;
    for (float64& value : seen)
    {
      value += noise(generator);
    }
    PliRecord made = record_at(range, seen, start_ns + since, start_ns + since + radar_latency_ns);
    made.set_source(ics::v1::PLI_SOURCE_SAPIENT);
    made.set_fix_type(PliRecord::FIX_TYPE_UNSPECIFIED);
    made.set_horizontal_sigma_m(radar_sigma_m);
    made.set_vertical_sigma_m(radar_sigma_m);
    if (chance(generator) >= radar_dropout)
    {
      records.push_back(made);
    }
  }
  return records;
}

ics::cue::Cue_settings settings_for(const ics::cue::Site& station)
{
  return {origin, station, ics::v1::ENTITY_ROLE_TARGET, ics::Duration(latency_ns), filter, defaults};
}

// The angle between a cue and the true direction from a station.
float64 error_of(const ics::cue::Cue& cue, const Vector3& truth)
{
  const Vector3 cued{std::sin(cue.azimuth_rad) * std::cos(cue.elevation_rad),
                     std::cos(cue.azimuth_rad) * std::cos(cue.elevation_rad),
                     std::sin(cue.elevation_rad)};
  const float64 length = std::hypot(truth[0], truth[1], truth[2]);
  const float64 along = ((cued[0] * truth[0]) + (cued[1] * truth[1]) + (cued[2] * truth[2])) / length;
  const float64 across_east = ((cued[1] * truth[2]) - (cued[2] * truth[1])) / length;
  const float64 across_north = ((cued[2] * truth[0]) - (cued[0] * truth[2])) / length;
  const float64 across_up = ((cued[0] * truth[1]) - (cued[1] * truth[0])) / length;
  return std::atan2(std::hypot(across_east, across_north, across_up), along);
}

// The largest and the RMS pointing error.
struct Errors
{
  // The largest error.
  float64 largest_rad;
  // The RMS error.
  float64 rms_rad;
  // How many cues were compared.
  int64 cues;
};

// Every cue for a station over a track, compared with the truth.
Errors fly(const Track& track,
           const std::vector<PliRecord>& arrivals,
           const ics::cue::Site& site)
{
  const ics::frames::EnuFrame range(point(origin));
  const ics::frames::EnuFrame station(point(site));
  ics::Result<ics::cue::Cue_extrapolator> made = ics::cue::Cue_extrapolator::make(settings_for(site));
  EXPECT_TRUE(made.has_value());
  Errors errors{0.0, 0.0, 0};
  float64 squares = 0.0;
  const int64 count = static_cast<int64>(arrivals.size());
  int64 next = 0;
  const int64 last_ns = ((static_cast<int64>(track.size()) - 1) * step_ns) - latency_ns;
  for (int64 since = 0; since <= last_ns; since += tick_ns)
  {
    while ((next < count) && (arrivals[static_cast<std::size_t>(next)].received_utc_ns() <= (start_ns + since)))
    {
      static_cast<void>(made->take(arrivals[static_cast<std::size_t>(next)]));
      ++next;
    }
    const ics::Result<ics::cue::Cue> cue = made->cue_at(ics::UtcTime{} + ics::Duration(start_ns + since));
    if (since >= settle_ns)
    {
      EXPECT_TRUE(cue.has_value()) << since;
      const ics::frames::Enu seen = station.to_enu(geodetic_of(range, truth_at(track, since + latency_ns).position));
      const Vector3 truth{seen.east.value(), seen.north.value(), seen.up.value()};
      const float64 error = cue.has_value() ? error_of(*cue, truth) : 1.0;
      errors.largest_rad = std::fmax(errors.largest_rad, error);
      squares += error * error;
      ++errors.cues;
    }
  }
  errors.rms_rad = std::sqrt(squares / static_cast<float64>(errors.cues));
  return errors;
}

std::vector<PliRecord> arrivals_of(const Track& track)
{
  const ics::frames::EnuFrame range(point(origin));
  std::vector<PliRecord> arrivals = pli_records(track, range);
  const std::vector<PliRecord> radar = radar_records(track, range);
  arrivals.insert(arrivals.end(), radar.begin(), radar.end());
  std::ranges::stable_sort(arrivals, {}, &PliRecord::received_utc_ns);
  return arrivals;
}

void expect_within_budget(const Track& track, const std::string& name)
{
  const std::vector<PliRecord> arrivals = arrivals_of(track);
  for (const ics::cue::Site& site : stations)
  {
    const Errors errors = fly(track, arrivals, site);
    std::cout << name << " from (" << site.latitude_deg << ", " << site.longitude_deg << "): " << errors.cues
              << " cues, largest " << (errors.largest_rad * per_milliradian) << " mrad, RMS "
              << (errors.rms_rad * per_milliradian)
              << " mrad\n";
    EXPECT_LT(errors.largest_rad, budget_rad) << name;
  }
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(CueReplay, PointsWithin2MilliradiansOnTheCrossing)
{
  expect_within_budget(crossing_track(crossing_s), "crossing");
}

TEST(CueReplay, PointsWithin2MilliradiansThroughTurnsOf1G)
{
  expect_within_budget(manoeuvring_track(manoeuvring_s), "manoeuvring");
}
