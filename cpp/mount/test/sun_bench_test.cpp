// The keep-out end to end (ICS-034): the interlock around a PWI4 client, the
// emulated PWI4 moving its axes in real time, and randomized gotos and rate
// demands, many aimed through the sun. The line of sight must never come
// within the 15° cone, and a clock or ephemeris fault must park the mount.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <random>
#include <thread>

#include <gtest/gtest.h>

#include "emulated_pwi4.hpp"
#include "ics/common/units.hpp"
#include "ics/mount/interlock.hpp"
#include "ics/mount/keep_out.hpp"
#include "ics/mount/sun.hpp"
#include "mount_support.hpp"

namespace {

using ics::Radians;
using ics::UtcTime;
using ics::mount::Action;
using ics::mount::AltAz;
using ics::mount::AxisRates;
using ics::mount::MountStatus;
using ics::mount::SunInterlock;
using ics::mount::testing::bench_site;
using ics::mount::testing::EmulatedAxis;
using ics::mount::testing::EmulatedPwi4;
using ics::mount::testing::TestClock;
using std::chrono::steady_clock;

constexpr double kDeg = std::numbers::pi / 180.0;
constexpr auto kTrial = std::chrono::seconds(3);
constexpr int kTries = 10000;

using Rng = std::mt19937_64;

double uniform(Rng& rng, const double low, const double high) { return std::uniform_real_distribution<>(low, high)(rng); }

ics::mount::Deadline soon() { return ics::mount::testing::soon(); }

// A time in 2026 to 2030 with the sun 10° to 70° up at the bench site.
UtcTime sunny(Rng& rng) {
  UtcTime time{};
  for (int i = 0; i < kTries; ++i) {
    time = ics::utc_from_ns(static_cast<std::int64_t>(uniform(rng, 1.767e18, 1.893e18)));
    const double altitude = ics::mount::sun_position(time, bench_site(), {}).value().altitude.value() / kDeg;
    if (altitude > 10.0 && altitude < 70.0) {
      break;
    }
  }
  return time;
}

// Axes pointing at least 17° from the sun, within the limits.
std::array<EmulatedAxis, 2> axes_clear_of(Rng& rng, const AltAz& sun) {
  auto axes = ics::mount::testing::default_axes();
  for (int i = 0; i < kTries; ++i) {
    axes[0].position_deg = uniform(rng, -180, 180);
    axes[1].position_deg = uniform(rng, 6, 84);
    const AltAz start{.altitude = Radians(axes[1].position_deg * kDeg), .azimuth = Radians(axes[0].position_deg * kDeg)};
    if (ics::mount::separation(start, sun).value() >= 17 * kDeg) {
      break;
    }
  }
  return axes;
}

// Rates at speed (°/s) straight at the sun, in axis coordinates.
AxisRates toward(const MountStatus& status, const AltAz& sun, const double speed) {
  const AltAz from = ics::mount::pointing(status);
  const double d_az = std::remainder(sun.azimuth.value() - from.azimuth.value(), 2.0 * std::numbers::pi);
  const double d_alt = sun.altitude.value() - from.altitude.value();
  const double norm = std::max(std::hypot(d_az, d_alt), 1e-9);
  return {.rad_per_s = {speed * kDeg * d_az / norm, speed * kDeg * d_alt / norm}};
}

// A goto target beyond the sun from where the mount points.
AltAz beyond(const MountStatus& status, const AltAz& sun) {
  const AltAz from = ics::mount::pointing(status);
  return {.altitude = Radians(std::clamp((2 * sun.altitude.value()) - from.altitude.value(), 6 * kDeg, 84 * kDeg)),
          .azimuth = Radians((2 * sun.azimuth.value()) - from.azimuth.value())};
}

struct Rig {
  EmulatedPwi4 pwi4;
  TestClock clock;
  SunInterlock interlock;

  Rig(const std::array<EmulatedAxis, 2>& axes, const UtcTime start)
      : pwi4(axes), clock(start), interlock(ics::mount::testing::interlock_for(pwi4, clock)) {
    // Connected, so PWI4's own distance to the sun is checked.
    EXPECT_TRUE(interlock.connect(soon()).has_value());
  }
};

enum class Command : std::uint8_t { kNone, kGoto, kRates };

// One period: supervise, then the command, unless the interlock acted.
void period(Rig& rig, Rng& rng, const Command command, std::uint64_t& gotos) {
  const auto status = rig.interlock.status(soon());
  ASSERT_TRUE(status.has_value());
  const auto acted = rig.interlock.supervise(*status, soon());
  ASSERT_TRUE(acted.has_value());
  if (*acted != Action::Kind::kNone || command == Command::kNone) {
    return;
  }
  const AltAz sun = rig.interlock.state().sun;
  if (command == Command::kGoto) {
    gotos += rig.interlock.goto_alt_az(beyond(*status, sun), soon()).has_value() ? 1 : 0;
    return;
  }
  ASSERT_TRUE(rig.interlock.set_rates(toward(*status, sun, uniform(rng, 2, 10)), *status, soon()).has_value());
}

// One trial: a goto beyond the sun, then rates straight at it, by turns,
// each for half a second, at 20 to 50 Hz.
double trial(Rng& rng, std::uint64_t& gotos) {
  const UtcTime start = sunny(rng);
  const AltAz sun = ics::mount::sun_position(start, bench_site(), {}).value();
  Rig rig(axes_clear_of(rng, sun), start);
  const auto step =
      std::chrono::duration_cast<steady_clock::duration>(std::chrono::duration<double>(1.0 / uniform(rng, 20, 50)));
  const steady_clock::time_point begin = steady_clock::now();
  steady_clock::time_point next = begin;
  std::int64_t last_phase = -1;
  while (next - begin < kTrial) {
    const std::int64_t phase = (next - begin) / std::chrono::milliseconds(500);
    const bool new_phase = phase != last_phase;
    last_phase = phase;
    const Command goto_or_none = new_phase ? Command::kGoto : Command::kNone;
    period(rig, rng, phase % 2 == 1 ? Command::kRates : goto_or_none, gotos);
    next += step;
    std::this_thread::sleep_until(next);
  }
  EXPECT_TRUE(rig.interlock.stop_rates(soon()).has_value());
  EXPECT_EQ(rig.interlock.state().fault, ics::mount::SunFault::kNone);
  return rig.pwi4.seen().closest_to_sun_deg;
}

TEST(SunBench, RandomizedCommandsThroughPwi4NeverEnterTheCone) {
  Rng rng(34);
  double closest = 180.0;
  std::uint64_t gotos = 0;
  for (int i = 0; i < 20; ++i) {
    const double trial_closest = trial(rng, gotos);
    EXPECT_GE(trial_closest, 15.0) << "trial " << i;
    closest = std::min(closest, trial_closest);
  }
  std::printf("closest approach %.3f deg over 20 trials; %llu gotos sent\n", closest,
              static_cast<unsigned long long>(gotos));
}

// Runs the interlock until the emulator has parked the mount, or 15 s pass.
bool parks(Rig& rig) {
  const steady_clock::time_point give_up = steady_clock::now() + std::chrono::seconds(15);
  while (steady_clock::now() < give_up) {
    const auto status = rig.interlock.status(soon());
    if (status && rig.interlock.supervise(*status, soon()) &&
        ics::mount::separation(rig.pwi4.pointing(), {.altitude = Radians(10 * kDeg), .azimuth = Radians(0.0)})
                .value() < 0.01 * kDeg) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

// The mount at azimuth −60°, 20° up, the sun far from the way north to park.
std::array<EmulatedAxis, 2> west_of_north() {
  auto axes = ics::mount::testing::default_axes();
  axes[0].position_deg = -60.0;
  axes[1].position_deg = 20.0;
  return axes;
}

UtcTime morning() { return ics::utc_from_ns(1782054000000000000); }

TEST(SunBench, AClockFaultParksTheMount) {
  Rig rig(west_of_north(), morning());
  ASSERT_TRUE(rig.interlock.enable(ics::mount::Axis::kAzimuth, soon()).has_value());
  const auto status = rig.interlock.status(soon());
  ASSERT_EQ(rig.interlock.supervise(status.value(), soon()).value(), Action::Kind::kNone);
  rig.clock.free_running();
  EXPECT_TRUE(parks(rig));
  EXPECT_EQ(rig.interlock.state().fault, ics::mount::SunFault::kClock);
  EXPECT_EQ(rig.pwi4.seen().parks, 1U);
  EXPECT_GE(rig.pwi4.seen().closest_to_sun_deg, 15.0);
}

TEST(SunBench, AnEphemerisFaultParksTheMount) {
  Rig rig(west_of_north(), morning());
  const auto status = rig.interlock.status(soon());
  ASSERT_EQ(rig.interlock.supervise(status.value(), soon()).value(), Action::Kind::kNone);
  // The clock jumps out of the ephemeris's span while reporting it is fine.
  rig.clock.jump(std::chrono::years(150));
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  EXPECT_TRUE(parks(rig));
  EXPECT_EQ(rig.interlock.state().fault, ics::mount::SunFault::kEphemeris);
  EXPECT_EQ(rig.pwi4.seen().parks, 1U);
  EXPECT_GE(rig.pwi4.seen().closest_to_sun_deg, 15.0);
}

}  // namespace
