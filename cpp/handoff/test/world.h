// world.h: a station's world for the scheduler's tests (ICS-039). Vehicles
// fly scripted paths in the range frame. Their PLI comes as the MAVLink
// adapter gives it: every 100 ms, in GLOBAL_POSITION_INT's 1e-7 degree and
// millimetre steps, received 100 ms late, each position off by GNSS noise of
// 0.5 m one-sigma east and north and 0.8 m up. A mount turns at the rates it is
// sent, 50 ms after each command, within a rate and an acceleration. The
// tracking camera's frames show every vehicle the mount points at, as a spot.
// The scheduler and its tracker are the real ones.
#ifndef ICS_HANDOFF_TEST_WORLD_H
#define ICS_HANDOFF_TEST_WORLD_H

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <frame_maker.h>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/extrapolator.h>
#include <ics/cue/filter.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/measurement.h>
#include <ics/cue/pointing.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/encoder/track.hpp>
#include <ics/frames/enu.hpp>
#include <ics/frames/geodetic.hpp>
#include <ics/handoff/scheduler.h>
#include <ics/mount/limits.hpp>
#include <ics/mount/pwi4_status.hpp>
#include <ics/mount/rate_loop.hpp>
#include <ics/tracker/camera.h>
#include <ics/tracker/centroid.h>
#include <ics/tracker/geometry.h>
#include <ics/tracker/tracker.h>
#include <ics/v1/common.pb.h>
#include <ics/v1/pli.pb.h>

namespace ics::handoff_test
{

using cue::Vector3;
using handoff::Approach_settings;
using handoff::Handoff_settings;
using handoff::Queue_settings;
using handoff::Scheduler;
using handoff::Slew_limits;

inline constexpr int64 start_ns = 1760000000000000000;
inline constexpr int64 frame_ns = 10000000;
inline constexpr int64 turn_ns = 1000000;
inline constexpr int64 pli_period_ns = 100000000;
inline constexpr int64 pli_latency_ns = 100000000;
inline constexpr int64 latency_ns = 50000000;
inline constexpr int64 processing_ns = 1000000;
inline constexpr int64 frames_a_command = 2;
inline constexpr float64 nanoseconds_per_second = 1.0E9;
inline constexpr float64 turn_s = 0.001;
// The mount: 10 degrees a second, 1 rad/s^2.
inline constexpr float64 mount_rate = std::numbers::pi / 18.0;
inline constexpr float64 mount_acceleration = 1.0;
// The camera: 128 by 96 pixels of 100 microradians.
inline constexpr int32 width = 128;
inline constexpr int32 height = 96;
inline constexpr float64 boresight_x = 64.0;
inline constexpr float64 boresight_y = 48.0;
inline constexpr float64 plate_scale = 1.0E-4;
// The central half of the field reaches a quarter of it either side of the boresight.
inline constexpr float64 central_part = 4.0;
inline constexpr float64 sky = 1000.0;
inline constexpr float64 spot_sigma = 1.2;
inline constexpr float64 spot_amplitude = 2000.0;
inline constexpr float64 k_sigma = 5.0;
inline constexpr float64 min_contrast = 100.0;
inline constexpr int32 max_area = 200;
inline constexpr int32 acquire_side = 80;
inline constexpr int32 track_side = 24;
inline constexpr float64 gate_px = 6.0;
inline constexpr int32 confirm_frames = 5;
inline constexpr float64 alpha = 0.5;
inline constexpr float64 beta = 0.1;
inline constexpr float64 tracker_gain = 2.0;
inline constexpr Duration coast = std::chrono::milliseconds(500);
// The PLI's steps and accuracies.
inline constexpr float64 degree_step = 1.0E7;
inline constexpr float64 millimetres = 1000.0;
inline constexpr float64 centimetres = 100.0;
inline constexpr float64 pli_horizontal_sigma_m = 0.5;
inline constexpr float64 pli_vertical_sigma_m = 0.8;
inline constexpr int64 noise_seed = 39;
inline constexpr cue::Fix_sigmas fallback{1.5, 3.0};
inline constexpr cue::Filter_settings filter{
  {4.0, 1.0}, 16.27, std::chrono::seconds(2), std::chrono::seconds(2), cue::max_history, 20.0, 10.0};
inline constexpr cue::Noise_defaults defaults{fallback, fallback, fallback, fallback, fallback, fallback, 0.2};
inline constexpr cue::Site origin{40.0, -100.0, 700.0};
// Three stations 250 to 300 m from the range origin.
inline constexpr std::array<cue::Site, 3> stations{
  {{39.9977477, -100.0, 702.0}, {40.0010812, -99.9969537, 702.0}, {40.0018919, -100.0021096, 702.0}}};
inline constexpr Approach_settings approach{3.0, std::chrono::seconds(60)};
inline constexpr Queue_settings queue{std::chrono::seconds(3),
                                      std::chrono::milliseconds(500),
                                      std::chrono::milliseconds(500),
                                      std::chrono::milliseconds(300),
                                      10.0};
inline constexpr Slew_limits slew{0.17, 0.5, 6.0};

// The tracker's settings in these worlds.
inline tracker::Tracker_settings tracking()
{
  return tracker::Tracker_settings{{boresight_x, boresight_y, plate_scale, 0.0, false},
                                   {tracker::Polarity::bright, k_sigma, min_contrast, 1, max_area},
                                   {alpha, beta},
                                   {acquire_side, acquire_side},
                                   {track_side, track_side},
                                   gate_px,
                                   confirm_frames,
                                   coast,
                                   Duration(latency_ns),
                                   tracker_gain};
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

// A vehicle: its name in the PLI, its flight, and whether the camera sees it.
struct Vehicle
{
  std::string name;
  Flight flight;
  bool shown;
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

inline frames::Geodetic point(const cue::Site& site)
{
  const Result<frames::Geodetic> made =
    frames::Geodetic::make(Degrees(site.latitude_deg), Degrees(site.longitude_deg), Meters(site.height_m));
  EXPECT_TRUE(made.has_value());
  return *made;
}

inline frames::Geodetic geodetic_of(const frames::EnuFrame& range, const Vector3& position)
{
  const Result<frames::Geodetic> found =
    range.to_geodetic({Meters(position[0]), Meters(position[1]), Meters(position[2])});
  EXPECT_TRUE(found.has_value());
  return *found;
}

// A record of a vehicle, as the MAVLink adapter gives it.
inline v1::PliRecord record_of(const frames::EnuFrame& range,
                               const std::string& name,
                               const Sample& sample,
                               const int64 valid_ns)
{
  const frames::Geodetic where = geodetic_of(range, sample.position);
  v1::PliRecord made;
  made.set_entity_id(name);
  made.set_valid_utc_ns(valid_ns);
  made.set_received_utc_ns(valid_ns + pli_latency_ns);
  made.set_source(v1::PLI_SOURCE_MAVLINK);
  made.set_fix_type(v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  made.set_horizontal_sigma_m(pli_horizontal_sigma_m);
  made.set_vertical_sigma_m(pli_vertical_sigma_m);
  made.mutable_position()->set_latitude_deg(std::round(where.latitude().value() * degree_step) / degree_step);
  made.mutable_position()->set_longitude_deg(std::round(where.longitude().value() * degree_step) / degree_step);
  made.mutable_position()->set_height_ellipsoid_m(std::round(where.height().value() * millimetres) / millimetres);
  const frames::EnuFrame here(where);
  const frames::Enu axes{Meters(sample.velocity[0]), Meters(sample.velocity[1]), Meters(sample.velocity[2])};
  const frames::Enu local = here.rotate(range.rotate(axes));
  made.mutable_velocity_enu_mps()->set_east(std::round(local.east.value() * centimetres) / centimetres);
  made.mutable_velocity_enu_mps()->set_north(std::round(local.north.value() * centimetres) / centimetres);
  made.mutable_velocity_enu_mps()->set_up(std::round(local.up.value() * centimetres) / centimetres);
  return made;
}

class Manual_time final : public tracker::Time_source
{
public:
  UtcTime now() override
  {
    return utc;
  }

  UtcTime utc{};
};

// A rate moved towards the one wanted, within the mount's rate and acceleration.
inline float64 turned(const float64 rate, const float64 wanted)
{
  const float64 allowed = std::clamp(wanted, -mount_rate, mount_rate);
  const float64 change = mount_acceleration * turn_s;
  return rate + std::clamp(allowed - rate, -change, change);
}

// A mount that turns at the rates it was last sent, once each command takes
// effect. Its axes are known at the time it was last seen, a frame's time.
class Sim_mount final : public tracker::Axis_source
{
public:
  Result<encoder::Interpolated> axes_at(const UtcTime when) override
  {
    Result<encoder::Interpolated> axes = fail(Error::kOutOfRange);
    if (known && (when == seen_.utc))
    {
      axes = seen_;
    }
    return axes;
  }

  void point(const encoder::Direction& direction)
  {
    azimuth_ = direction.azimuth_rad;
    elevation_ = direction.elevation_rad;
  }

  void see(const UtcTime when)
  {
    seen_.utc = when;
    seen_.encoder = pointing();
    seen_.azimuth_rate_rad_per_s = azimuth_rate_;
    seen_.elevation_rate_rad_per_s = elevation_rate_;
  }

  void command(const mount::RateDemand& demand)
  {
    pending_.push_back(mount::RateDemand{demand.rates, demand.time + Duration(latency_ns)});
  }

  // Turns for a millisecond that ends at a time.
  void turn(const UtcTime until)
  {
    while ((next_ < static_cast<int64>(pending_.size())) && (pending_[static_cast<std::size_t>(next_)].time <= until))
    {
      wanted_ = pending_[static_cast<std::size_t>(next_)].rates;
      ++next_;
    }
    azimuth_rate_ = turned(azimuth_rate_, wanted_.rad_per_s[static_cast<std::size_t>(mount::Axis::kAzimuth)]);
    elevation_rate_ = turned(elevation_rate_, wanted_.rad_per_s[static_cast<std::size_t>(mount::Axis::kAltitude)]);
    azimuth_ += azimuth_rate_ * turn_s;
    elevation_ += elevation_rate_ * turn_s;
  }

  encoder::Direction pointing() const
  {
    return encoder::Direction{encoder::wrap_turn(azimuth_), elevation_};
  }

  bool known = true;

private:
  float64 azimuth_ = 0.0;
  float64 elevation_ = 0.0;
  float64 azimuth_rate_ = 0.0;
  float64 elevation_rate_ = 0.0;
  mount::AxisRates wanted_{};
  std::vector<mount::RateDemand> pending_;
  int64 next_ = 0;
  encoder::Interpolated seen_{};
};

// Two engagements, as the M-2 replay flies them. The first target crosses
// east at 5 m/s and the second north-west at 4 m/s, 60 to 70 m apart. Each
// interceptor is parked until 5 s before its hit, gathers speed over 2 s, and
// passes through its target on time.
inline constexpr Flight first_target{{-65.0, 0.0, 35.0}, {-25.0, 0.0, 35.0}, 0.0, 8.0, 0.0};
inline constexpr Flight second_target{{64.0, -10.0, 45.0}, {31.0, 17.5, 45.0}, 0.0, 11.0, 0.0};
inline constexpr Vector3 first_launch{-60.0, -120.0, 2.0};
inline constexpr Vector3 second_launch{80.0, -100.0, 2.0};
inline constexpr float64 flight_s = 5.0;
inline constexpr float64 ramp_s = 2.0;
// The vehicles' places in the list: each target, then its interceptor.
inline constexpr std::size_t first_target_place = 0U;
inline constexpr std::size_t second_target_place = 2U;

inline std::vector<Vehicle> two_engagements(const float64 first_hit_s, const float64 second_hit_s)
{
  const Vector3 first_hit = sample_of(first_target, first_hit_s).position;
  const Vector3 second_hit = sample_of(second_target, second_hit_s).position;
  return {{"uas-1", first_target, true},
          {"int-1", {first_launch, first_hit, first_hit_s - flight_s, first_hit_s, ramp_s}, true},
          {"uas-2", second_target, true},
          {"int-2", {second_launch, second_hit, second_hit_s - flight_s, second_hit_s, ramp_s}, true}};
}

// The scheduler's settings for a station and a run plan that pairs the
// vehicles as they are listed: a target, then its interceptor.
inline Handoff_settings settings_for(const cue::Site& station, const std::vector<Vehicle>& vehicles)
{
  Handoff_settings settings{};
  settings.cue = {origin, station, v1::ENTITY_ROLE_TARGET, Duration(latency_ns), filter, defaults};
  settings.count = static_cast<int32>(vehicles.size()) / 2;
  for (int32 engagement = 0; engagement < settings.count; ++engagement)
  {
    const std::string& target = vehicles[static_cast<std::size_t>(2 * engagement)].name;
    const std::string& interceptor = vehicles[static_cast<std::size_t>((2 * engagement) + 1)].name;
    settings.pairings[static_cast<std::size_t>(engagement)] = {target, interceptor};
  }
  settings.approach = approach;
  settings.queue = queue;
  settings.slew = slew;
  return settings;
}

// A station, its mount and its camera, among vehicles.
class Station_world
{
public:
  Station_world(const cue::Site& site, const std::vector<Vehicle>& vehicles)
      : vehicles_(vehicles), range_(point(origin)), station_(cue::station_at(range_, point(site)))
  {
    const std::array<encoder::Coefficient, 0> none{};
    model_ = *encoder::PointingModel::make(none);
    Result<std::unique_ptr<Scheduler>> made =
      Scheduler::make(settings_for(site, vehicles), tracking(), *model_, mount, clock);
    EXPECT_TRUE(made.has_value());
    scheduler_ = std::move(*made);
    clock.utc = UtcTime{} + Duration(start_ns);
  }

  Scheduler& scheduler()
  {
    return *scheduler_;
  }

  // The time since the start, in nanoseconds and in seconds.
  int64 since() const
  {
    return since_ns_;
  }

  float64 seconds() const
  {
    return static_cast<float64>(since_ns_) / nanoseconds_per_second;
  }

  UtcTime now() const
  {
    return UtcTime{} + Duration(start_ns + since_ns_);
  }

  // Where a vehicle truly is from the station now; nothing for one over the station.
  std::optional<encoder::Direction> direction_of(const std::size_t vehicle) const
  {
    const Sample sample = sample_of(vehicles_[vehicle].flight, seconds());
    cue::Estimate truth{};
    truth.time = now();
    truth.state = {sample.position[0], sample.position[1], sample.position[2], 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    const Result<cue::Cue> cue = cue::cue_for(truth, station_);
    std::optional<encoder::Direction> direction;
    if (cue.has_value())
    {
      direction = encoder::Direction{cue->azimuth_rad, cue->elevation_rad};
    }
    return direction;
  }

  // The pixel a vehicle falls on now, when it is in the frame.
  std::optional<tracker::Pixel_point> pixel_of(const std::size_t vehicle) const
  {
    const std::optional<encoder::Direction> direction = direction_of(vehicle);
    Result<tracker::Pixel_point> pixel = fail(Error::kEmpty);
    if (direction.has_value())
    {
      pixel = tracker::pixel_of_sky(*direction, mount.pointing(), tracking().camera);
    }
    std::optional<tracker::Pixel_point> inside;
    if (pixel.has_value())
    {
      const bool across = (pixel->x >= 0.0) && (pixel->x < static_cast<float64>(width));
      if (across && (pixel->y >= 0.0) && (pixel->y < static_cast<float64>(height)))
      {
        inside = *pixel;
      }
    }
    return inside;
  }

  // Whether a vehicle is in the central half of the field, across and down.
  bool centred(const std::size_t vehicle) const
  {
    const std::optional<tracker::Pixel_point> pixel = pixel_of(vehicle);
    bool central = false;
    if (pixel.has_value())
    {
      const bool across = std::abs(pixel->x - boresight_x) <= (static_cast<float64>(width) / central_part);
      central = across && (std::abs(pixel->y - boresight_y) <= (static_cast<float64>(height) / central_part));
    }
    return central;
  }

  // Gives the scheduler the reports received now.
  void report()
  {
    if ((since_ns_ % pli_period_ns) == 0)
    {
      const int64 valid_ns = since_ns_ - pli_latency_ns;
      for (const Vehicle& vehicle : vehicles_)
      {
        Sample sample = sample_of(vehicle.flight, static_cast<float64>(valid_ns) / nanoseconds_per_second);
        if (noisy)
        {
          sample.position[0] += horizontal_(generator_);
          sample.position[1] += horizontal_(generator_);
          sample.position[2] += vertical_(generator_);
        }
        static_cast<void>(scheduler_->take(record_of(range_, vehicle.name, sample, start_ns + valid_ns)));
      }
    }
  }

  // The frame now, with every vehicle shown that the mount points at.
  tracker::Frame frame()
  {
    maker_ = tracker_test::Frame_maker(width, height, sky);
    for (int32 number = 0; number < static_cast<int32>(vehicles_.size()); ++number)
    {
      const std::size_t vehicle = static_cast<std::size_t>(number);
      const std::optional<tracker::Pixel_point> pixel = pixel_of(vehicle);
      if (vehicles_[vehicle].shown && pixel.has_value())
      {
        maker_.spot(pixel->x, pixel->y, spot_sigma, spot_amplitude);
      }
    }
    return maker_.frame(now());
  }

  // One frame on: the mount turns, the reports due arrive, and the camera
  // gives its frame, a millisecond before the scheduler has it.
  tracker::Frame next_frame()
  {
    for (int64 turn = 0; turn < (frame_ns / turn_ns); ++turn)
    {
      since_ns_ += turn_ns;
      mount.turn(now());
    }
    if (reporting)
    {
      report();
    }
    mount.see(now());
    clock.utc = now() + Duration(processing_ns);
    return frame();
  }

  // One frame on: the scheduler takes the next frame, and every other frame
  // the mount is sent its demand.
  tracker::Take tick()
  {
    const tracker::Take taken = scheduler_->take(next_frame());
    const std::optional<mount::RateDemand> demand = scheduler_->latest();
    if ((((since_ns_ / frame_ns) % frames_a_command) == 0) && demand.has_value())
    {
      mount.command(*demand);
    }
    return taken;
  }

  // Frames until a time since the start.
  void run_until(const int64 since_ns)
  {
    while (since_ns_ < since_ns)
    {
      static_cast<void>(tick());
    }
  }

  Sim_mount mount;
  Manual_time clock;
  bool reporting = true;
  bool noisy = true;

private:
  std::vector<Vehicle> vehicles_;
  frames::EnuFrame range_;
  cue::Station station_;
  std::optional<encoder::PointingModel> model_;
  std::unique_ptr<Scheduler> scheduler_;
  tracker_test::Frame_maker maker_{width, height, sky};
  // Every station hears the same reports, so every world draws the same noise.
  std::mt19937_64 generator_{static_cast<std::mt19937_64::result_type>(noise_seed)};
  std::normal_distribution<float64> horizontal_{0.0, pli_horizontal_sigma_m};
  std::normal_distribution<float64> vertical_{0.0, pli_vertical_sigma_m};
  int64 since_ns_ = 0;
};

}  // namespace ics::handoff_test

#endif  // ICS_HANDOFF_TEST_WORLD_H
