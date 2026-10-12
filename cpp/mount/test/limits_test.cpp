#include "ics/mount/limits.hpp"

#include <chrono>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>

#include <gtest/gtest.h>

namespace {

using ics::Radians;
using ics::mount::AltAz;
using ics::mount::AxisRates;
using ics::mount::MountLimits;
using ics::mount::MountStatus;

constexpr double kDeg = std::numbers::pi / 180.0;
constexpr std::size_t kAz = 0;
constexpr std::size_t kAlt = 1;

// Azimuth within ±270°, altitude 5° to 85°, both at most 10°/s.
MountLimits limits() {
  MountLimits out;
  out.axes[kAz] = {.min = Radians(-270 * kDeg), .max = Radians(270 * kDeg), .max_rate_rad_per_s = 10 * kDeg};
  out.axes[kAlt] = {.min = Radians(5 * kDeg), .max = Radians(85 * kDeg), .max_rate_rad_per_s = 10 * kDeg};
  return out;
}

// The mount at azimuth az and altitude alt, its own altitude limits 0° to
// 80°.
MountStatus at(const double az_deg, const double alt_deg) {
  MountStatus out;
  out.axes[kAz] = {.enabled = true,
                   .position = Radians(az_deg * kDeg),
                   .velocity_rad_per_s = 0.0,
                   .min_position = Radians(-360 * kDeg),
                   .max_position = Radians(360 * kDeg)};
  out.axes[kAlt] = {.enabled = true,
                    .position = Radians(alt_deg * kDeg),
                    .velocity_rad_per_s = 0.0,
                    .min_position = Radians(0.0),
                    .max_position = Radians(80 * kDeg)};
  return out;
}

AxisRates rates(const double az_deg_s, const double alt_deg_s) {
  return AxisRates{.rad_per_s = {az_deg_s * kDeg, alt_deg_s * kDeg}};
}

TEST(MountLimits, AllowsAGotoWithinTheNarrowerAltitudeLimits) {
  const MountStatus status = at(0, 30);
  EXPECT_TRUE(ics::mount::check_goto({.altitude = Radians(45 * kDeg), .azimuth = Radians(500 * kDeg)}, status,
                                     limits())
                  .has_value());
  // Below the configured 5°, and above the mount's 80°.
  EXPECT_EQ(ics::mount::check_goto({.altitude = Radians(4 * kDeg), .azimuth = Radians(0)}, status, limits()).error(),
            ics::Error::kOutOfRange);
  EXPECT_EQ(ics::mount::check_goto({.altitude = Radians(81 * kDeg), .azimuth = Radians(0)}, status, limits()).error(),
            ics::Error::kOutOfRange);
}

TEST(MountLimits, PassesRatesWithinTheLimits) {
  const auto applied = ics::mount::limit_rates(rates(-5, 3), at(0, 30), limits());
  EXPECT_DOUBLE_EQ(applied.rates.rad_per_s[kAz], -5 * kDeg);
  EXPECT_DOUBLE_EQ(applied.rates.rad_per_s[kAlt], 3 * kDeg);
  EXPECT_FALSE(applied.clamped[kAz]);
  EXPECT_FALSE(applied.stopped_at_limit[kAlt]);
}

TEST(MountLimits, ClampsARateToItsMaximum) {
  const auto applied = ics::mount::limit_rates(rates(25, -12), at(0, 30), limits());
  EXPECT_DOUBLE_EQ(applied.rates.rad_per_s[kAz], 10 * kDeg);
  EXPECT_DOUBLE_EQ(applied.rates.rad_per_s[kAlt], -10 * kDeg);
  EXPECT_TRUE(applied.clamped[kAz]);
  EXPECT_TRUE(applied.clamped[kAlt]);
}

TEST(MountLimits, StopsAnAxisShortOfALimit) {
  // 2°/s up from 79° would pass the mount's 80° within the second; down from
  // 6° would pass the configured 5°.
  const auto up = ics::mount::limit_rates(rates(0, 2), at(0, 79), limits());
  EXPECT_EQ(up.rates.rad_per_s[kAlt], 0.0);
  EXPECT_TRUE(up.stopped_at_limit[kAlt]);
  const auto down = ics::mount::limit_rates(rates(-9, -2), at(-265, 6), limits());
  EXPECT_EQ(down.rates.rad_per_s[kAlt], 0.0);
  EXPECT_TRUE(down.stopped_at_limit[kAlt]);
  EXPECT_EQ(down.rates.rad_per_s[kAz], 0.0);
  EXPECT_TRUE(down.stopped_at_limit[kAz]);
  // Away from the limit it is past, an axis may always move.
  const auto back = ics::mount::limit_rates(rates(0, -2), at(0, 82), limits());
  EXPECT_DOUBLE_EQ(back.rates.rad_per_s[kAlt], -2 * kDeg);
  EXPECT_FALSE(back.stopped_at_limit[kAlt]);
}

TEST(MountLimits, TakesADemandThatIsNotANumberAsZero) {
  const auto applied = ics::mount::limit_rates(
      AxisRates{.rad_per_s = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}},
      at(0, 30), limits());
  EXPECT_EQ(applied.rates.rad_per_s[kAz], 0.0);
  EXPECT_EQ(applied.rates.rad_per_s[kAlt], 0.0);
  EXPECT_TRUE(applied.clamped[kAz]);
  EXPECT_TRUE(applied.clamped[kAlt]);
}

TEST(MountLimits, StopsAnAxisWithANegativeMaximum) {
  MountLimits bad = limits();
  bad.axes[kAz].max_rate_rad_per_s = -1.0;
  testing::internal::CaptureStderr();
  const auto applied = ics::mount::limit_rates(rates(5, 0), at(0, 30), bad);
  EXPECT_NE(testing::internal::GetCapturedStderr().find("ICS check failed"), std::string::npos);
  EXPECT_EQ(applied.rates.rad_per_s[kAz], 0.0);
}

}  // namespace
