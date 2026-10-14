#include "ics/mount/interlock.hpp"

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <thread>

#include <gtest/gtest.h>

#include "emulated_pwi4.hpp"
#include "ics/common/units.hpp"
#include "ics/mount/keep_out.hpp"
#include "ics/mount/limits.hpp"
#include "ics/mount/rate_loop.hpp"
#include "ics/mount/sun.hpp"
#include "mount_support.hpp"

namespace {

using ics::Radians;
using ics::UtcTime;
using ics::mount::Action;
using ics::mount::AltAz;
using ics::mount::AxisRates;
using ics::mount::ClockHealth;
using ics::mount::Instant;
using ics::mount::InterlockSettings;
using ics::mount::MountLimits;
using ics::mount::MountStatus;
using ics::mount::SunFault;
using ics::mount::SunGuard;
using ics::mount::testing::bench_site;
using ics::mount::testing::EmulatedPwi4;
using ics::mount::testing::interlock_settings;
using ics::mount::testing::TestClock;

constexpr double kDeg = std::numbers::pi / 180.0;
constexpr std::size_t kAz = 0;
constexpr std::size_t kAlt = 1;
using Kind = Action::Kind;

// 09:00 local time on 21 June 2026 at the bench site: the sun 35.3° up, at
// azimuth 82.6°.
UtcTime morning() { return ics::utc_from_ns(1782054000000000000); }

AltAz sun() { return ics::mount::sun_position(morning(), bench_site(), {}).value(); }

Instant at_s(const double seconds) {
  const auto since = std::chrono::duration_cast<ics::Duration>(std::chrono::duration<double>(seconds));
  return {.steady = std::chrono::steady_clock::time_point{} + since, .utc = morning() + since};
}

// A healthy report, a new one each second.
ClockHealth good(const double seconds) {
  return {.traceable = true,
          .error_bound = std::chrono::microseconds(1),
          .reported = morning() + std::chrono::seconds(static_cast<int>(seconds))};
}

MountLimits limits(const double min_alt_deg = 5.0) {
  MountLimits out;
  out.axes[kAz] = {.min = Radians(-270 * kDeg), .max = Radians(270 * kDeg), .max_rate_rad_per_s = 10 * kDeg};
  out.axes[kAlt] = {.min = Radians(min_alt_deg * kDeg), .max = Radians(85 * kDeg), .max_rate_rad_per_s = 10 * kDeg};
  return out;
}

// The mount at azimuth az and altitude alt in degrees, turning at these
// speeds in °/s.
MountStatus mount_at(const double alt_deg, const double az_deg, const double az_deg_s = 0.0,
                     const double alt_deg_s = 0.0) {
  MountStatus out;
  out.axes[kAz] = {.enabled = true,
                   .position = Radians(az_deg * kDeg),
                   .velocity_rad_per_s = az_deg_s * kDeg,
                   .min_position = Radians(-270 * kDeg),
                   .max_position = Radians(270 * kDeg)};
  out.axes[kAlt] = {.enabled = true,
                    .position = Radians(alt_deg * kDeg),
                    .velocity_rad_per_s = alt_deg_s * kDeg,
                    .min_position = Radians(0.0),
                    .max_position = Radians(90 * kDeg)};
  return out;
}

// The mount straight below the sun by below degrees, climbing at alt °/s.
MountStatus below_sun(const double below_deg, const double alt_deg_s = 0.0) {
  return mount_at((sun().altitude.value() / kDeg) - below_deg, sun().azimuth.value() / kDeg, 0.0, alt_deg_s);
}

AxisRates rates(const double az_deg_s, const double alt_deg_s) {
  return {.rad_per_s = {az_deg_s * kDeg, alt_deg_s * kDeg}};
}

SunGuard guard_for(const InterlockSettings& settings = interlock_settings(), const MountLimits& lim = limits()) {
  return SunGuard::make(bench_site(), settings, lim).value();
}

// Far from the sun, with the way north to park clear.
MountStatus far_away() { return mount_at(10, 300); }

TEST(SunGuard, RefusesAParkPositionTheSunCanNear) {
  InterlockSettings settings = interlock_settings();
  settings.park = {.altitude = Radians(30 * kDeg), .azimuth = Radians(180 * kDeg)};
  EXPECT_EQ(SunGuard::make(bench_site(), settings, limits()).error(), ics::Error::kInvalidArgument);
}

TEST(SunGuard, WaitsForTheFirstClockReport) {
  SunGuard guard = guard_for();
  EXPECT_EQ(guard.supervise(far_away(), at_s(0), std::nullopt).kind, Kind::kNone);
  EXPECT_FALSE(guard.state().ready);
  EXPECT_TRUE(guard.gate(rates(1, 0), far_away()).preempted);
  EXPECT_EQ(guard.check_goto({.altitude = Radians(10 * kDeg), .azimuth = Radians(0.0)}).error(),
            ics::Error::kUnavailable);
}

TEST(SunGuard, AllowsCommandsClearOfTheSun) {
  SunGuard guard = guard_for();
  const MountStatus status = below_sun(25);
  EXPECT_EQ(guard.supervise(status, at_s(0), good(0)).kind, Kind::kNone);
  EXPECT_TRUE(guard.state().ready);
  EXPECT_NEAR(guard.state().separation.value() / kDeg, 25.0, 1e-6);
  // Down and away, or round slowly, are clear; up at 10°/s comes within 17°.
  EXPECT_FALSE(guard.gate(rates(0, -5), status).preempted);
  EXPECT_FALSE(guard.gate(rates(1, 0), status).preempted);
  const ics::mount::GatedRates refused = guard.gate(rates(0, 10), status);
  EXPECT_TRUE(refused.preempted);
  EXPECT_NEAR(refused.rates.rad_per_s[kAlt], 0.0, 1e-12);
  EXPECT_EQ(guard.state().refused_rates, 1U);
  EXPECT_TRUE(guard.check_goto({.altitude = Radians(10 * kDeg), .azimuth = Radians(0.0)}).has_value());
  EXPECT_EQ(guard.check_goto(sun()).error(), ics::Error::kOutOfRange);
}

TEST(SunGuard, StopsAMotionHeadedIntoTheGuardZone) {
  SunGuard guard = guard_for();
  EXPECT_EQ(guard.supervise(below_sun(25, 10), at_s(0), good(0)).kind, Kind::kStop);
  EXPECT_TRUE(guard.state().active);
  EXPECT_EQ(guard.state().stops, 1U);
  EXPECT_EQ(guard.supervise(below_sun(25), at_s(0.5), good(0.5)).kind, Kind::kNone);
  EXPECT_FALSE(guard.state().active);
}

TEST(SunGuard, EscapesFromInsideTheGuardZone) {
  SunGuard guard = guard_for();
  const Action first = guard.supervise(below_sun(16), at_s(0), good(0));
  ASSERT_EQ(first.kind, Kind::kEscape);
  EXPECT_LT(first.rates.rad_per_s[kAlt], 0.0);
  EXPECT_TRUE(guard.state().escaping);
  // While escaping, nothing else may move the mount.
  EXPECT_TRUE(guard.gate(rates(0, -1), below_sun(16)).preempted);
  EXPECT_EQ(guard.check_goto({.altitude = Radians(10 * kDeg), .azimuth = Radians(0.0)}).error(),
            ics::Error::kUnavailable);
  EXPECT_EQ(guard.supervise(below_sun(17.5), at_s(0.5), good(0.5)).kind, Kind::kEscape);
  // A degree outside the zone, it stops and hands back.
  EXPECT_EQ(guard.supervise(below_sun(18.5), at_s(2), good(2)).kind, Kind::kStop);
  EXPECT_FALSE(guard.state().escaping);
  EXPECT_EQ(guard.state().escapes, 2U);
  EXPECT_EQ(guard.supervise(below_sun(18.5), at_s(2.5), good(2.5)).kind, Kind::kNone);
}

TEST(SunGuard, FaultsWhenTheLimitsBlockEveryEscape) {
  // Altitude limited to 20° and up, the mount at 20° below the sun: the way
  // away is down. The way north to park passes the sun too, so it holds.
  SunGuard guard = guard_for(interlock_settings(), limits(20.0));
  const MountStatus status = mount_at(20, sun().azimuth.value() / kDeg);
  EXPECT_EQ(guard.supervise(status, at_s(0), good(0)).kind, Kind::kStop);
  EXPECT_EQ(guard.state().fault, SunFault::kBlocked);
  EXPECT_FALSE(guard.state().parking);
}

TEST(SunGuard, ParksOnAClockFault) {
  SunGuard guard = guard_for();
  ASSERT_EQ(guard.supervise(far_away(), at_s(0), good(0)).kind, Kind::kNone);
  ClockHealth free = good(1);
  free.traceable = false;
  EXPECT_EQ(guard.supervise(far_away(), at_s(1), free).kind, Kind::kPark);
  EXPECT_EQ(guard.state().fault, SunFault::kClock);
  EXPECT_TRUE(guard.state().parking);
  EXPECT_FALSE(guard.state().ready);
}

TEST(SunGuard, FaultsOnALargeErrorBound) {
  SunGuard guard = guard_for();
  ClockHealth loose = good(0);
  loose.error_bound = std::chrono::seconds(2);
  // No good sun yet, so it holds.
  EXPECT_EQ(guard.supervise(far_away(), at_s(0), loose).kind, Kind::kStop);
  EXPECT_EQ(guard.state().fault, SunFault::kClock);
  EXPECT_FALSE(guard.state().parking);
}

TEST(SunGuard, FaultsWhenReportsStop) {
  SunGuard guard = guard_for();
  ASSERT_EQ(guard.supervise(far_away(), at_s(0), good(0)).kind, Kind::kNone);
  EXPECT_EQ(guard.supervise(far_away(), at_s(30), good(0)).kind, Kind::kNone);
  EXPECT_EQ(guard.supervise(far_away(), at_s(31), good(0)).kind, Kind::kPark);
  EXPECT_EQ(guard.state().fault, SunFault::kClock);
}

TEST(SunGuard, FaultsWhenNoReportEverComes) {
  SunGuard guard = guard_for();
  EXPECT_EQ(guard.supervise(far_away(), at_s(0), std::nullopt).kind, Kind::kNone);
  EXPECT_EQ(guard.supervise(far_away(), at_s(31), std::nullopt).kind, Kind::kStop);
  EXPECT_EQ(guard.state().fault, SunFault::kClock);
}

TEST(SunGuard, FaultsOnATimeOutsideTheEphemeris) {
  SunGuard guard = guard_for();
  ASSERT_EQ(guard.supervise(far_away(), at_s(0), good(0)).kind, Kind::kNone);
  // Within a second the sun is not worked out again.
  Instant soon = at_s(0.5);
  soon.utc += std::chrono::years(150);
  EXPECT_EQ(guard.supervise(far_away(), soon, good(0.5)).kind, Kind::kNone);
  Instant later = at_s(2);
  later.utc += std::chrono::years(150);
  EXPECT_EQ(guard.supervise(far_away(), later, good(2)).kind, Kind::kPark);
  EXPECT_EQ(guard.state().fault, SunFault::kEphemeris);
}

TEST(SunGuard, FaultsOnASunThatIsNotANumber) {
  InterlockSettings settings = interlock_settings();
  settings.sun.pressure_mbar = std::numeric_limits<double>::quiet_NaN();
  SunGuard guard = guard_for(settings);
  EXPECT_EQ(guard.supervise(far_away(), at_s(0), good(0)).kind, Kind::kStop);
  EXPECT_EQ(guard.state().fault, SunFault::kEphemeris);
}

TEST(SunGuard, FaultsWhenPwi4Disagrees) {
  SunGuard guard = guard_for();
  MountStatus status = below_sun(30);
  // Not connected: PWI4 reports 0, and it is not compared.
  ASSERT_EQ(guard.supervise(status, at_s(0), good(0)).kind, Kind::kNone);
  status.connected = true;
  status.distance_to_sun = Radians(30.5 * kDeg);
  ASSERT_EQ(guard.supervise(status, at_s(0.5), good(0.5)).kind, Kind::kNone);
  status.distance_to_sun = Radians(35 * kDeg);
  EXPECT_NE(guard.supervise(status, at_s(1), good(1)).kind, Kind::kNone);
  EXPECT_EQ(guard.state().fault, SunFault::kEphemeris);
}

TEST(SunGuard, HoldsWhenTheWayToParkPassesTheSun) {
  SunGuard guard = guard_for();
  // Up and round the far side of the sun from north: the way to park sweeps
  // across it.
  const MountStatus status = mount_at(60, 170);
  ASSERT_EQ(guard.supervise(status, at_s(0), good(0)).kind, Kind::kNone);
  ClockHealth free = good(1);
  free.traceable = false;
  EXPECT_EQ(guard.supervise(status, at_s(1), free).kind, Kind::kStop);
  EXPECT_FALSE(guard.state().parking);
}

TEST(SunGuard, StopsAParkSlewHeadedForTheSun) {
  SunGuard guard = guard_for();
  ASSERT_EQ(guard.supervise(far_away(), at_s(0), good(0)).kind, Kind::kNone);
  ClockHealth free = good(1);
  free.traceable = false;
  ASSERT_EQ(guard.supervise(far_away(), at_s(1), free).kind, Kind::kPark);
  // Slewing away is watched and left alone; slewing at the sun is stopped.
  EXPECT_EQ(guard.supervise(far_away(), at_s(1.5), free).kind, Kind::kNone);
  EXPECT_EQ(guard.supervise(below_sun(25, 10), at_s(2), free).kind, Kind::kStop);
  EXPECT_FALSE(guard.state().parking);
  // Holding, nothing more is done.
  EXPECT_EQ(guard.supervise(below_sun(25, 10), at_s(2.5), free).kind, Kind::kNone);
}

TEST(SunGuard, LatchesAFaultUntilCleared) {
  SunGuard guard = guard_for();
  ClockHealth free = good(0);
  free.traceable = false;
  ASSERT_EQ(guard.supervise(far_away(), at_s(0), free).kind, Kind::kStop);
  EXPECT_EQ(guard.supervise(far_away(), at_s(1), good(1)).kind, Kind::kNone);
  EXPECT_EQ(guard.state().fault, SunFault::kClock);
  EXPECT_FALSE(guard.state().ready);
  guard.clear_fault();
  EXPECT_EQ(guard.supervise(far_away(), at_s(2), good(2)).kind, Kind::kNone);
  EXPECT_EQ(guard.state().fault, SunFault::kNone);
  EXPECT_TRUE(guard.state().ready);
}

// The emulated PWI4 with its axes at alt and az, and its sun where the
// interlock will work it out to be.
struct Rig {
  EmulatedPwi4 pwi4;
  TestClock clock{morning()};
  ics::mount::SunInterlock interlock;

  Rig(const double alt_deg, const double az_deg)
      : pwi4(axes(alt_deg, az_deg)), interlock(ics::mount::testing::interlock_for(pwi4, clock)) {}

  static std::array<ics::mount::testing::EmulatedAxis, 2> axes(const double alt_deg, const double az_deg) {
    auto out = ics::mount::testing::default_axes();
    out[0].position_deg = az_deg;
    out[1].position_deg = alt_deg;
    return out;
  }

  ics::Result<Kind> supervise() {
    const auto status = interlock.status(ics::mount::testing::soon());
    return interlock.supervise(status.value(), ics::mount::testing::soon());
  }
};

double sun_alt_deg() { return sun().altitude.value() / kDeg; }
double sun_az_deg() { return sun().azimuth.value() / kDeg; }

TEST(SunInterlock, RefusesAParkPositionTheSunCanNear) {
  EmulatedPwi4 pwi4;
  TestClock clock(morning());
  InterlockSettings settings = interlock_settings();
  settings.park = {.altitude = Radians(30 * kDeg), .azimuth = Radians(180 * kDeg)};
  const auto made = ics::mount::SunInterlock::make(ics::mount::testing::client_for(pwi4), clock, bench_site(), settings);
  EXPECT_EQ(made.error(), ics::Error::kInvalidArgument);
}

TEST(SunInterlock, SendsNothingWhenClear) {
  Rig rig(10, 300);
  EXPECT_EQ(rig.supervise().value(), Kind::kNone);
  EXPECT_EQ(rig.pwi4.seen().requests, 1U);
}

TEST(SunInterlock, SendsAnEscape) {
  Rig rig(sun_alt_deg() - 16, sun_az_deg());
  EXPECT_EQ(rig.supervise().value(), Kind::kEscape);
  EXPECT_EQ(rig.pwi4.seen().rate_commands, 1U);
  EXPECT_LT(rig.pwi4.seen().last_rate_deg_s[1], 0.0);
}

TEST(SunInterlock, StopsAndParksOnAFault) {
  Rig rig(10, 300);
  ASSERT_EQ(rig.supervise().value(), Kind::kNone);
  rig.clock.free_running();
  EXPECT_EQ(rig.supervise().value(), Kind::kPark);
  EXPECT_EQ(rig.pwi4.seen().stop_rates, 1U);
  EXPECT_EQ(rig.pwi4.seen().stops, 1U);
  EXPECT_EQ(rig.pwi4.seen().parks, 1U);
  EXPECT_EQ(rig.interlock.state().fault, SunFault::kClock);
  rig.interlock.clear_fault();
  EXPECT_EQ(rig.interlock.state().fault, SunFault::kNone);
}

TEST(SunInterlock, ReportsAStopThatFailed) {
  Rig rig(sun_alt_deg() - 25, sun_az_deg());
  rig.clock.free_running();
  const auto status = rig.interlock.status(ics::mount::testing::soon());
  // Every attempt at zero rates fails; the stop is still sent.
  rig.pwi4.fail_next(3);
  EXPECT_EQ(rig.interlock.supervise(status.value(), ics::mount::testing::soon()).error(), ics::Error::kUnavailable);
  EXPECT_EQ(rig.pwi4.seen().stops, 1U);
}

TEST(SunInterlock, ReportsAnEscapeThatFailed) {
  Rig rig(sun_alt_deg() - 16, sun_az_deg());
  const auto status = rig.interlock.status(ics::mount::testing::soon());
  rig.pwi4.fail_next(3);
  EXPECT_EQ(rig.interlock.supervise(status.value(), ics::mount::testing::soon()).error(), ics::Error::kUnavailable);
}

TEST(SunInterlock, GatesGotos) {
  Rig rig(10, 300);
  ASSERT_EQ(rig.supervise().value(), Kind::kNone);
  EXPECT_EQ(rig.interlock.goto_alt_az(sun(), ics::mount::testing::soon()).error(), ics::Error::kOutOfRange);
  EXPECT_EQ(rig.pwi4.seen().gotos, 0U);
  EXPECT_TRUE(rig.interlock
                  .goto_alt_az({.altitude = Radians(20 * kDeg), .azimuth = Radians(330 * kDeg)},
                               ics::mount::testing::soon())
                  .has_value());
  EXPECT_EQ(rig.pwi4.seen().gotos, 1U);
}

TEST(SunInterlock, GatesRates) {
  Rig rig(sun_alt_deg() - 20, sun_az_deg());
  const MountStatus status = rig.interlock.status(ics::mount::testing::soon()).value();
  ASSERT_EQ(rig.interlock.supervise(status, ics::mount::testing::soon()).value(), Kind::kNone);
  const auto up = rig.interlock.set_rates(rates(0, 10), status, ics::mount::testing::soon());
  ASSERT_TRUE(up.has_value());
  EXPECT_TRUE(up->preempted);
  EXPECT_NEAR(rig.pwi4.seen().last_rate_deg_s[1], 0.0, 1e-12);
  const auto down = rig.interlock.set_rates(rates(0, -30), status, ics::mount::testing::soon());
  ASSERT_TRUE(down.has_value());
  EXPECT_FALSE(down->preempted);
  EXPECT_TRUE(down->applied.clamped[kAlt]);
  EXPECT_NEAR(rig.pwi4.seen().last_rate_deg_s[1], -10.0, 1e-6);
  rig.pwi4.fail_next(3);
  EXPECT_EQ(rig.interlock.set_rates(rates(0, -1), status, ics::mount::testing::soon()).error(),
            ics::Error::kUnavailable);
  EXPECT_TRUE(rig.interlock.stop_rates(ics::mount::testing::soon()).has_value());
  EXPECT_TRUE(rig.interlock.stop(ics::mount::testing::soon()).has_value());
  EXPECT_EQ(rig.interlock.client_stats().failures, 1U);
}

// Runs the loop until stop_after has passed, or it stops by itself.
ics::Status run_for(ics::mount::RateLoop& loop, const std::chrono::milliseconds stop_after) {
  std::atomic<bool> stop{false};
  std::thread stopper([&stop, stop_after] {
    std::this_thread::sleep_for(stop_after);
    stop = true;
  });
  const ics::Status status = loop.run(stop);
  stopper.join();
  return status;
}

TEST(SunInterlock, TakesTheMountFromTheRateLoopOnAFault) {
  Rig rig(10, 300);
  rig.clock.free_running();
  ics::mount::testing::FixedSource source(ics::mount::testing::fresh(1, 0));
  ics::mount::RateLoop loop(rig.interlock, source, {.rate_hz = 50.0});
  EXPECT_EQ(run_for(loop, std::chrono::milliseconds(500)).error(), ics::Error::kUnavailable);
  EXPECT_EQ(loop.stats().fault, ics::mount::Fault::kInterlock);
  // The interlock's stop, and nothing from the loop after it.
  EXPECT_EQ(rig.pwi4.seen().stops, 1U);
  EXPECT_EQ(rig.pwi4.seen().rate_commands, 0U);
}

// A demand made now, each time it is asked.
class Climb final : public ics::mount::RateSource {
 public:
  [[nodiscard]] std::optional<ics::mount::RateDemand> latest() override {
    return ics::mount::testing::fresh(0, 10);
  }
};

TEST(SunInterlock, CountsPeriodsItPreemptsInTheRateLoop) {
  // Inside the guard zone: every period escapes.
  Rig inside(sun_alt_deg() - 16, sun_az_deg());
  Climb climb;
  ics::mount::RateLoop escaping(inside.interlock, climb, {.rate_hz = 50.0});
  ASSERT_TRUE(run_for(escaping, std::chrono::milliseconds(300)).has_value());
  EXPECT_EQ(escaping.stats().preempted, escaping.stats().periods);
  EXPECT_LT(inside.pwi4.seen().max_abs_rate_deg_s[1], 2.0);
  // 25° below it, climbing at 10°/s is refused each period.
  Rig below(sun_alt_deg() - 25, sun_az_deg());
  ics::mount::RateLoop climbing(below.interlock, climb, {.rate_hz = 50.0});
  ASSERT_TRUE(run_for(climbing, std::chrono::milliseconds(300)).has_value());
  EXPECT_EQ(climbing.stats().preempted, climbing.stats().periods);
  EXPECT_NEAR(below.pwi4.seen().max_abs_rate_deg_s[1], 0.0, 1e-12);
}

}  // namespace
