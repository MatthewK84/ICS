// slew_test.cpp: the slew's closing rate, its demand and its time (ICS-039).
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>

#include <gtest/gtest.h>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/handoff/slew.h>
#include <ics/tracker/demand.h>
#include <ics/tracker/estimator.h>

namespace
{

using ics::float64;
using ics::encoder::Direction;
using ics::handoff::closing_rate;
using ics::handoff::reach_seconds;
using ics::handoff::slew_seconds;
using ics::handoff::Slew_limits;
using ics::tracker::Angle_state;
using ics::tracker::Axis_rates;

constexpr float64 rate_limit = 0.17;
constexpr float64 acceleration = 0.5;
constexpr float64 gain = 6.0;
constexpr Slew_limits limits{rate_limit, acceleration, gain};
constexpr float64 knee = acceleration / (gain * gain);
constexpr float64 small_error = 0.001;
constexpr float64 large_error = 0.3;
constexpr float64 azimuth = 1.0;
constexpr float64 elevation = 0.5;
constexpr float64 far_azimuth = 2.0;
constexpr float64 low_elevation = 0.1;
constexpr float64 azimuth_rate = 0.02;
constexpr float64 elevation_rate = -0.01;
constexpr float64 fast_rate = 0.3;
constexpr float64 near_north = 0.005;
constexpr float64 short_angle = 0.02;
constexpr float64 first_azimuth = 0.1;
constexpr float64 first_elevation = 0.3;
constexpr float64 turned_azimuth = 0.6;
constexpr float64 risen_elevation = 0.9;
constexpr float64 barely_turned = 0.12;
constexpr float64 barely_risen = 0.35;
constexpr float64 turn = 0.5;
constexpr float64 rise = 0.6;
constexpr float64 across_north = 0.2;
constexpr float64 tolerance = 1.0E-12;
constexpr ics::Duration lead = std::chrono::milliseconds(100);

const ics::UtcTime now = ics::UtcTime{} + std::chrono::seconds(1);

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Slew, ASmallErrorIsClosedAtTheGain)
{
  EXPECT_NEAR(closing_rate(small_error, limits), gain * small_error, tolerance);
  EXPECT_NEAR(closing_rate(-small_error, limits), -gain * small_error, tolerance);
  EXPECT_NEAR(closing_rate(knee, limits), gain * knee, tolerance);
  EXPECT_DOUBLE_EQ(closing_rate(0.0, limits), 0.0);
}

TEST(Slew, ALargeErrorIsClosedAtTheRateASteadyDecelerationStopsFrom)
{
  const float64 expected = std::sqrt((2.0 * acceleration) * (large_error - (knee / 2.0)));
  EXPECT_NEAR(closing_rate(large_error, limits), expected, tolerance);
  EXPECT_NEAR(closing_rate(-large_error, limits), -expected, tolerance);
}

TEST(Slew, AMountOnItsTargetIsAskedForTheTargetsRates)
{
  const Angle_state target{now, azimuth, elevation, azimuth_rate, elevation_rate};
  const Axis_rates rates = ics::handoff::slew_demand(target, target, limits);
  EXPECT_NEAR(rates.azimuth_rad_s, azimuth_rate, tolerance);
  EXPECT_NEAR(rates.elevation_rad_s, elevation_rate, tolerance);
}

TEST(Slew, ADemandIsHeldWithinTheRateLimit)
{
  const Angle_state mount{now, azimuth, elevation, 0.0, 0.0};
  const Axis_rates far = ics::handoff::slew_demand({now, far_azimuth, low_elevation, 0.0, 0.0}, mount, limits);
  EXPECT_DOUBLE_EQ(far.azimuth_rad_s, rate_limit);
  EXPECT_DOUBLE_EQ(far.elevation_rad_s, -rate_limit);
  const Axis_rates fast = ics::handoff::slew_demand({now, azimuth, elevation, fast_rate, -fast_rate}, mount, limits);
  EXPECT_DOUBLE_EQ(fast.azimuth_rad_s, rate_limit);
  EXPECT_DOUBLE_EQ(fast.elevation_rad_s, -rate_limit);
}

TEST(Slew, GoesTheShortWayAcrossNorth)
{
  const Angle_state mount{now, near_north, elevation, 0.0, 0.0};
  const Angle_state target{now, (2.0 * std::numbers::pi) - near_north, elevation, 0.0, 0.0};
  const Axis_rates rates = ics::handoff::slew_demand(target, mount, limits);
  EXPECT_NEAR(rates.azimuth_rad_s, -gain * (2.0 * near_north), tolerance);
  EXPECT_DOUBLE_EQ(rates.elevation_rad_s, 0.0);
}

TEST(Slew, TakesTheErrorWhereTheMountWillBe)
{
  const Angle_state mount{now, azimuth, elevation, azimuth_rate, elevation_rate};
  const Axis_rates rates = ics::handoff::slew_demand({now + lead, azimuth, elevation, 0.0, 0.0}, mount, limits);
  const float64 seconds = std::chrono::duration<float64>(lead).count();
  EXPECT_NEAR(rates.azimuth_rad_s, -gain * (azimuth_rate * seconds), tolerance);
  EXPECT_NEAR(rates.elevation_rad_s, -gain * (elevation_rate * seconds), tolerance);
}

TEST(Slew, AShortSlewNeverReachesTheRateLimit)
{
  EXPECT_NEAR(slew_seconds(short_angle, limits), 2.0 * std::sqrt(short_angle / acceleration), tolerance);
  EXPECT_DOUBLE_EQ(slew_seconds(-short_angle, limits), slew_seconds(short_angle, limits));
  EXPECT_DOUBLE_EQ(slew_seconds(0.0, limits), 0.0);
}

TEST(Slew, ALongSlewCruisesAtTheRateLimit)
{
  EXPECT_NEAR(slew_seconds(1.0, limits), (1.0 / rate_limit) + (rate_limit / acceleration), tolerance);
}

TEST(Slew, AMountReachesADirectionWhenItsSlowerAxisDoes)
{
  const Direction first{first_azimuth, first_elevation};
  EXPECT_NEAR(reach_seconds(first, {turned_azimuth, barely_risen}, limits), slew_seconds(turn, limits), tolerance);
  EXPECT_NEAR(reach_seconds(first, {barely_turned, risen_elevation}, limits), slew_seconds(rise, limits), tolerance);
  const Direction west_of_north{(2.0 * std::numbers::pi) - first_azimuth, first_elevation};
  EXPECT_NEAR(reach_seconds(first, west_of_north, limits), slew_seconds(across_north, limits), tolerance);
}

TEST(Slew, LimitsMustBePositive)
{
  EXPECT_TRUE(ics::handoff::valid(limits));
  EXPECT_FALSE(ics::handoff::valid(Slew_limits{0.0, acceleration, gain}));
  EXPECT_FALSE(ics::handoff::valid(Slew_limits{rate_limit, -acceleration, gain}));
  EXPECT_FALSE(ics::handoff::valid(Slew_limits{rate_limit, acceleration, 0.0}));
  EXPECT_FALSE(ics::handoff::valid(Slew_limits{std::numeric_limits<float64>::quiet_NaN(), acceleration, gain}));
}
