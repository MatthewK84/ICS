// The keep-out trials (ICS-034): randomized gotos and rate demands, many aimed
// through the sun, against a simulated mount in simulated time, with the
// interlock's SunGuard deciding as it does against PWI4. The line of sight
// must never come within the 15° cone.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <cstdio>
#include <random>
#include <string>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mount/interlock.hpp"
#include "ics/mount/keep_out.hpp"
#include "ics/mount/limits.hpp"
#include "ics/mount/sun.hpp"
#include "sim_mount.hpp"

namespace {

using ics::Degrees;
using ics::Meters;
using ics::Radians;
using ics::UtcTime;
using ics::frames::Geodetic;
using ics::mount::Action;
using ics::mount::AltAz;
using ics::mount::AxisRates;
using ics::mount::ClockHealth;
using ics::mount::InterlockSettings;
using ics::mount::MountLimits;
using ics::mount::MountStatus;
using ics::mount::separation;
using ics::mount::SunFault;
using ics::mount::SunGuard;
using ics::mount::testing::SimMount;

constexpr double kDeg = std::numbers::pi / 180.0;
constexpr double kDt = 0.002;
constexpr std::size_t kAz = 0;
constexpr std::size_t kAlt = 1;

using Rng = std::mt19937_64;

double uniform(Rng& rng, const double low, const double high) { return std::uniform_real_distribution<>(low, high)(rng); }
bool chance(Rng& rng, const double p) { return uniform(rng, 0.0, 1.0) < p; }

MountLimits limits() {
  MountLimits out;
  out.axes[kAz] = {.min = Radians(-270 * kDeg), .max = Radians(270 * kDeg), .max_rate_rad_per_s = 10 * kDeg};
  out.axes[kAlt] = {.min = Radians(5 * kDeg), .max = Radians(85 * kDeg), .max_rate_rad_per_s = 10 * kDeg};
  return out;
}

// The sun where it truly is, worked out again every 0.5 s: it moves under
// 0.003° in that time.
struct Sky {
  Geodetic site;
  UtcTime start{};
  AltAz sun{};
  double sun_at = -1.0;

  AltAz at(const double t) {
    if (t - sun_at >= 0.5) {
      sun = ics::mount::sun_position(start + std::chrono::duration_cast<ics::Duration>(std::chrono::duration<double>(t)),
                                     site, {})
                .value();
      sun_at = t;
    }
    return sun;
  }
};

// Tries at a random pick before taking whatever the last try gave.
constexpr int kTries = 10000;

// A site and a time with the sun between 5° below the horizon and 80° up.
Sky random_sky(Rng& rng) {
  for (int i = 1;; ++i) {
    Sky sky{.site = Geodetic::make(Degrees(uniform(rng, -50, 50)), Degrees(uniform(rng, -180, 180)),
                                   Meters(uniform(rng, 0, 2000)))
                        .value(),
            .start = ics::utc_from_ns(static_cast<std::int64_t>(uniform(rng, 9.47e17, 4.10e18)))};
    const double altitude = sky.at(0.0).altitude.value() / kDeg;
    if ((altitude > -5.0 && altitude < 80.0) || i == kTries) {
      return sky;
    }
  }
}

// The pole-ward horizon, 10° up: outside the guard zone all year below 50°.
InterlockSettings settings_for(const Geodetic& site) {
  InterlockSettings out;
  out.park = {.altitude = Radians(10 * kDeg), .azimuth = Radians(site.latitude().value() >= 0.0 ? 0.0 : std::numbers::pi)};
  return out;
}

// A start outside the cone: one trial in ten inside the guard zone, which
// the interlock must escape.
AltAz random_start(Rng& rng, const AltAz& sun) {
  const bool inside = chance(rng, 0.1);
  for (int i = 1;; ++i) {
    const AltAz start{.altitude = Radians(uniform(rng, 5, 85) * kDeg), .azimuth = Radians(uniform(rng, -180, 180) * kDeg)};
    const double apart = separation(start, sun).value() / kDeg;
    if ((inside ? (apart > 15.5 && apart < 17.0) : apart >= 17.0) || i == kTries) {
      return start;
    }
  }
}

SimMount random_mount(Rng& rng, const AltAz& start) {
  const double acceleration = uniform(rng, 20, 60) * kDeg;
  return SimMount({{{.max_rate = 10 * kDeg, .acceleration = acceleration, .min = -270 * kDeg, .max = 270 * kDeg},
                    {.max_rate = 10 * kDeg, .acceleration = acceleration, .min = 0.0, .max = 90 * kDeg}}},
                  start);
}

// Rates at speed (rad/s) straight at the sun, in axis coordinates.
AxisRates toward(const AltAz& from, const AltAz& sun, const double speed) {
  const double d_az = std::remainder(sun.azimuth.value() - from.azimuth.value(), 2.0 * std::numbers::pi);
  const double d_alt = sun.altitude.value() - from.altitude.value();
  const double norm = std::max(std::hypot(d_az, d_alt), 1e-9);
  return {.rad_per_s = {speed * d_az / norm, speed * d_alt / norm}};
}

// A goto target: beyond the sun from here, inside the cone, or anywhere.
AltAz random_target(Rng& rng, const AltAz& from, const AltAz& sun) {
  const double pick = uniform(rng, 0, 1);
  if (pick < 0.5) {
    const double d_az = std::remainder(sun.azimuth.value() - from.azimuth.value(), 2.0 * std::numbers::pi);
    return {.altitude = Radians(std::clamp((2 * sun.altitude.value()) - from.altitude.value(), 6 * kDeg, 84 * kDeg)),
            .azimuth = Radians(sun.azimuth.value() + d_az)};
  }
  if (pick < 0.75) {
    return {.altitude = Radians(std::clamp(sun.altitude.value() + (uniform(rng, -10, 10) * kDeg), 6 * kDeg, 84 * kDeg)),
            .azimuth = Radians(sun.azimuth.value() + (uniform(rng, -10, 10) * kDeg))};
  }
  return {.altitude = Radians(uniform(rng, 6, 84) * kDeg), .azimuth = Radians(uniform(rng, 0, 360) * kDeg)};
}

// What a trial did.
struct Outcome {
  double closest_deg = 180.0;
  std::uint64_t refused_gotos = 0;
  std::uint64_t stops = 0;
  std::uint64_t refused_rates = 0;
  std::uint64_t escapes = 0;
  SunFault fault = SunFault::kNone;
  bool parked = false;
};

// A fault to inject part way through a trial.
enum class Inject : std::uint8_t { kNone, kNotTraceable, kErrorBound, kTimeJump, kDisagreement };

// One trial: the commands of each phase, the guard each period, and the
// mount and the truth each step.
class Trial {
 public:
  Trial(Rng& rng, const Inject inject, const double length_s)
      : rng_(rng),
        sky_(random_sky(rng)),
        settings_(settings_for(sky_.site)),
        guard_(SunGuard::make(sky_.site, settings_, limits()).value()),
        mount_(random_mount(rng, random_start(rng, sky_.at(0.0)))),
        period_(1.0 / uniform(rng, 20, 50)),
        inject_(inject),
        inject_at_(uniform(rng, 1, 4)),
        length_s_(length_s) {}

  Outcome run() {
    double next_period = 0.0;
    for (double t = 0.0; t < length_s_; t += kDt) {
      const AltAz sun = sky_.at(t);
      out_.closest_deg = std::min(out_.closest_deg, separation(mount_.pointing(), sun).value() / kDeg);
      if (t >= next_period) {
        period(t, sun);
        next_period += period_;
      }
      mount_.step(kDt);
    }
    const ics::mount::InterlockState& state = guard_.state();
    out_.stops = state.stops;
    out_.refused_rates = state.refused_rates;
    out_.escapes = state.escapes;
    out_.fault = state.fault;
    out_.parked = separation(mount_.pointing(), settings_.park).value() < 0.1 * kDeg;
    return out_;
  }

 private:
  void period(const double t, const AltAz& sun) {
    const bool faulty = inject_ != Inject::kNone && t >= inject_at_;
    MountStatus status = mount_.status(sun);
    status.distance_to_sun = Radians(status.distance_to_sun.value() +
                                     (faulty && inject_ == Inject::kDisagreement ? 5 * kDeg : 0.0));
    const auto since = std::chrono::duration_cast<ics::Duration>(std::chrono::duration<double>(t));
    const UtcTime utc = sky_.start + since + (faulty && inject_ == Inject::kTimeJump ? std::chrono::years(150) : ics::Duration{});
    const ClockHealth health{.traceable = !(faulty && inject_ == Inject::kNotTraceable),
                             .error_bound = faulty && inject_ == Inject::kErrorBound ? std::chrono::seconds(10)
                                                                                     : std::chrono::microseconds(1),
                             .reported = sky_.start + std::chrono::duration_cast<std::chrono::seconds>(since)};
    const Action action = guard_.supervise(status, {.steady = steady0_ + since, .utc = utc}, health);
    act(action, status, t);
  }

  void act(const Action& action, const MountStatus& status, const double t) {
    if (action.kind == Action::Kind::kStop) {
      mount_.stop();
    } else if (action.kind == Action::Kind::kEscape) {
      mount_.set_rates(ics::mount::limit_rates(action.rates, status, limits()).rates);
    } else if (action.kind == Action::Kind::kPark) {
      mount_.slew(settings_.park, chance(rng_, 0.5));
    } else if (guard_.state().fault == SunFault::kNone) {
      command(status, t);
    }
  }

  // The phase's command: a goto at its start, or its rates each period.
  void command(const MountStatus& status, const double t) {
    if (t >= phase_end_) {
      phase_end_ = t + uniform(rng_, 0.5, 2.0);
      goto_phase_ = chance(rng_, 0.4);
      at_sun_ = chance(rng_, 0.6);
      speed_ = uniform(rng_, 1, 10) * kDeg;
      fixed_ = toward(ics::mount::pointing(status),
                      {.altitude = Radians(uniform(rng_, 5, 85) * kDeg), .azimuth = Radians(uniform(rng_, 0, 360) * kDeg)},
                      speed_);
      if (goto_phase_) {
        start_goto(status);
      }
    }
    if (!goto_phase_) {
      const AxisRates demand = at_sun_ ? toward(ics::mount::pointing(status), guard_.state().sun, speed_) : fixed_;
      mount_.set_rates(guard_.gate(ics::mount::limit_rates(demand, status, limits()).rates, status).rates);
    }
  }

  void start_goto(const MountStatus& status) {
    const AltAz target = random_target(rng_, ics::mount::pointing(status), guard_.state().sun);
    if (!guard_.check_goto(target) || !ics::mount::check_goto(target, status, limits())) {
      ++out_.refused_gotos;
      return;
    }
    mount_.slew(target, chance(rng_, 0.5));
  }

  Rng& rng_;
  Sky sky_;
  InterlockSettings settings_;
  SunGuard guard_;
  SimMount mount_;
  double period_;
  Inject inject_;
  double inject_at_;
  double length_s_;
  std::chrono::steady_clock::time_point steady0_{};
  Outcome out_;
  double phase_end_ = 0.0;
  bool goto_phase_ = false;
  bool at_sun_ = false;
  double speed_ = 0.0;
  AxisRates fixed_;
};

TEST(SunTrials, RandomizedCommandsNeverEnterTheCone) {
  Rng rng(34);
  Outcome total;
  for (int i = 0; i < 10000; ++i) {
    const Outcome trial = Trial(rng, Inject::kNone, 8.0).run();
    ASSERT_GE(trial.closest_deg, 15.0) << "trial " << i;
    EXPECT_TRUE(trial.fault == SunFault::kNone || trial.fault == SunFault::kBlocked) << "trial " << i;
    total.closest_deg = std::min(total.closest_deg, trial.closest_deg);
    total.refused_gotos += trial.refused_gotos;
    total.stops += trial.stops;
    total.refused_rates += trial.refused_rates;
    total.escapes += trial.escapes;
  }
  // The commands pressed on the sun: many were refused, stopped or escaped.
  EXPECT_GT(total.refused_gotos, 1000U);
  EXPECT_GT(total.stops, 1000U);
  EXPECT_GT(total.refused_rates, 1000U);
  EXPECT_GT(total.escapes, 100U);
  RecordProperty("closest_deg", std::to_string(total.closest_deg));
  std::printf("closest approach %.3f deg; refused gotos %llu, stops %llu, refused rate periods %llu, escapes %llu\n",
              total.closest_deg, static_cast<unsigned long long>(total.refused_gotos),
              static_cast<unsigned long long>(total.stops), static_cast<unsigned long long>(total.refused_rates),
              static_cast<unsigned long long>(total.escapes));
}

TEST(SunTrials, AFaultParksTheMountOrHoldsItOutsideTheCone) {
  Rng rng(3434);
  const std::array<Inject, 4> faults{Inject::kNotTraceable, Inject::kErrorBound, Inject::kTimeJump,
                                     Inject::kDisagreement};
  std::uint64_t parked = 0;
  std::uint64_t trials = 0;
  for (int i = 0; i < 125; ++i) {
    for (const Inject inject : faults) {
      const Outcome trial = Trial(rng, inject, 60.0).run();
      ASSERT_GE(trial.closest_deg, 15.0) << "trial " << i;
      ASSERT_NE(trial.fault, SunFault::kNone) << "trial " << i;
      parked += trial.parked ? 1 : 0;
      ++trials;
    }
  }
  // Most faults find the way to park clear; the rest hold.
  EXPECT_GT(parked * 2, trials);
  std::printf("parked after %llu of %llu faults\n", static_cast<unsigned long long>(parked),
              static_cast<unsigned long long>(trials));
}

}  // namespace
