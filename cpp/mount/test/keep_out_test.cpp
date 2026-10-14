#include "ics/mount/keep_out.hpp"

#include <chrono>
#include <cstddef>
#include <numbers>
#include <optional>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mount/limits.hpp"
#include "ics/mount/pwi4_status.hpp"

namespace {

using ics::Degrees;
using ics::Meters;
using ics::Radians;
using ics::frames::Geodetic;
using ics::mount::AltAz;
using ics::mount::AxisRates;
using ics::mount::KeepOut;
using ics::mount::MountLimits;
using ics::mount::MountStatus;
using ics::mount::separation;
using ics::mount::Sweep;

constexpr double kDeg = std::numbers::pi / 180.0;
constexpr std::size_t kAz = 0;
constexpr std::size_t kAlt = 1;
constexpr auto kSecond = std::chrono::seconds(1);

AltAz at(const double alt_deg, const double az_deg) {
  return {.altitude = Radians(alt_deg * kDeg), .azimuth = Radians(az_deg * kDeg)};
}

double degrees(const Radians angle) { return angle.value() / kDeg; }

// The mount at azimuth az and altitude alt, turning at these speeds in °/s.
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

AxisRates rates(const double az_deg_s, const double alt_deg_s) {
  return {.rad_per_s = {az_deg_s * kDeg, alt_deg_s * kDeg}};
}

// Azimuth within ±270°, altitude 5° to 85°, both at most 10°/s.
MountLimits limits() {
  MountLimits out;
  out.axes[kAz] = {.min = Radians(-270 * kDeg), .max = Radians(270 * kDeg), .max_rate_rad_per_s = 10 * kDeg};
  out.axes[kAlt] = {.min = Radians(5 * kDeg), .max = Radians(85 * kDeg), .max_rate_rad_per_s = 10 * kDeg};
  return out;
}

TEST(KeepOut, GuardsTheConeAndTheMargin) { EXPECT_NEAR(degrees(ics::mount::guard(KeepOut{})), 17.0, 1e-12); }

TEST(KeepOut, MeasuresSeparations) {
  EXPECT_NEAR(degrees(separation(at(30, 40), at(30, 40))), 0.0, 1e-9);
  EXPECT_NEAR(degrees(separation(at(0, 0), at(0, 90))), 90.0, 1e-9);
  EXPECT_NEAR(degrees(separation(at(90, 0), at(0, 123))), 90.0, 1e-9);
  // An azimuth counted on past a turn is the same direction.
  EXPECT_NEAR(degrees(separation(at(20, 359), at(20, -1))), 0.0, 1e-9);
  EXPECT_NEAR(degrees(separation(at(10, 0), at(-10, 180))), 180.0, 1e-9);
}

TEST(KeepOut, FindsTheClosestApproachOverASweep) {
  const auto sweep = [](const double alt_low, const double alt_high, const double az_low, const double az_high) {
    return Sweep{.altitude_low = Radians(alt_low * kDeg),
                 .altitude_high = Radians(alt_high * kDeg),
                 .azimuth_low = Radians(az_low * kDeg),
                 .azimuth_high = Radians(az_high * kDeg)};
  };
  // The sun inside the sweep.
  EXPECT_NEAR(degrees(closest_approach(sweep(0, 40, 80, 120), at(20, 100))), 0.0, 1e-9);
  // On the horizon, nearer the high edge, then nearer the low edge, counted
  // the other way round.
  EXPECT_NEAR(degrees(closest_approach(sweep(0, 0, 80, 120), at(0, 130))), 10.0, 1e-9);
  EXPECT_NEAR(degrees(closest_approach(sweep(0, 0, 80, 120), at(0, 70 - 360))), 10.0, 1e-9);
  // A full turn covers every azimuth: only altitude separates them.
  EXPECT_NEAR(degrees(closest_approach(sweep(10, 20, -200, 200), at(50, 33))), 30.0, 1e-9);
  // Above the sweep at an azimuth within it.
  EXPECT_NEAR(degrees(closest_approach(sweep(0, 20, 80, 120), at(45, 100))), 25.0, 1e-9);
  // A sweep upside down is a bug; it is taken to reach the sun.
  EXPECT_NEAR(degrees(closest_approach(sweep(20, 0, 80, 120), at(45, 100))), 0.0, 1e-12);
}

TEST(KeepOut, SweepsAheadAlongVelocityAndRates) {
  const Sweep ahead = ics::mount::sweep_ahead(mount_at(30, 100, 2, -1), rates(-5, 3), kSecond);
  EXPECT_NEAR(degrees(ahead.azimuth_low), 95.0, 1e-9);
  EXPECT_NEAR(degrees(ahead.azimuth_high), 102.0, 1e-9);
  EXPECT_NEAR(degrees(ahead.altitude_low), 29.0, 1e-9);
  EXPECT_NEAR(degrees(ahead.altitude_high), 33.0, 1e-9);
}

TEST(KeepOut, ClearsMotionAwayFromTheSunAndStopsMotionIntoIt) {
  const AltAz sun = at(30, 90);
  const KeepOut keep_out;
  // Far away, slowly.
  EXPECT_TRUE(clear_ahead(mount_at(30, 180), rates(1, 0), sun, keep_out, kSecond));
  // 20° away, heading in at 5°/s: within 17° inside a second.
  EXPECT_FALSE(clear_ahead(mount_at(30, 113), rates(-5, 0), sun, keep_out, kSecond));
  // Coasting in, with no rate commanded.
  EXPECT_FALSE(clear_ahead(mount_at(30, 113, -5, 0), AxisRates{}, sun, keep_out, kSecond));
  // Inside the guard zone, leaving it.
  EXPECT_TRUE(clear_ahead(mount_at(30, 105), rates(5, 0), sun, keep_out, kSecond));
}

TEST(KeepOut, RefusesAGotoIntoTheGuardZone) {
  const AltAz sun = at(40, 150);
  EXPECT_EQ(check_goto_sun(at(40, 166), sun, KeepOut{}).error(), ics::Error::kOutOfRange);
  EXPECT_TRUE(check_goto_sun(at(40, 175), sun, KeepOut{}).has_value());
}

TEST(KeepOut, EscapesStraightAwayFromTheSun) {
  const AltAz sun = at(30, 90);
  const MountStatus status = mount_at(32, 100);
  const std::optional<AxisRates> away = ics::mount::escape_rates(status, sun, limits(), KeepOut{});
  ASSERT_TRUE(away.has_value());
  // Further round in azimuth and up, at 1°/s on the sky.
  EXPECT_GT(away->rad_per_s[kAz], 0.0);
  EXPECT_GT(away->rad_per_s[kAlt], 0.0);
  const AltAz next{.altitude = Radians(32 * kDeg + away->rad_per_s[kAlt]),
                   .azimuth = Radians(100 * kDeg + away->rad_per_s[kAz])};
  EXPECT_NEAR(degrees(separation(next, sun)) - degrees(separation(ics::mount::pointing(status), sun)), 1.0, 0.05);
}

TEST(KeepOut, EscapesDownwardFromTheSunItself) {
  const std::optional<AxisRates> away = ics::mount::escape_rates(mount_at(40, 120), at(40, 120), limits(), KeepOut{});
  ASSERT_TRUE(away.has_value());
  EXPECT_NEAR(away->rad_per_s[kAz], 0.0, 1e-12);
  EXPECT_NEAR(away->rad_per_s[kAlt], -kDeg, 1e-12);
}

TEST(KeepOut, FindsNoEscapeWhenTheLimitsBlockIt) {
  // At the lowest altitude, with the sun straight above: the way away is
  // down, past the limit.
  EXPECT_FALSE(ics::mount::escape_rates(mount_at(5, 200), at(15, 200), limits(), KeepOut{}).has_value());
}

TEST(KeepOut, KnowsDirectionsTheSunNeverNears) {
  const Geodetic range = Geodetic::make(Degrees(32.38), Degrees(-106.48), Meters(1200)).value();
  // Due north, 10° up: declination 68°.
  EXPECT_TRUE(ics::mount::sun_safe_all_year(at(10, 0), range, KeepOut{}));
  // Due south, 30° up: declination −28°, which the sun passes near.
  EXPECT_FALSE(ics::mount::sun_safe_all_year(at(30, 180), range, KeepOut{}));
  // Far north, the midnight sun comes near the northern horizon.
  const Geodetic north = Geodetic::make(Degrees(65.0), Degrees(-147.0), Meters(200)).value();
  EXPECT_FALSE(ics::mount::sun_safe_all_year(at(5, 0), north, KeepOut{}));
}

}  // namespace
