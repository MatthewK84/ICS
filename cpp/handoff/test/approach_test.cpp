// approach_test.cpp: closest approaches found and not found (ICS-039).
#include <chrono>
#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>
#include <ics/handoff/approach.h>

namespace
{

using ics::float64;
using ics::cue::Estimate;
using ics::cue::Vector3;
using ics::handoff::Approach;
using ics::handoff::Approach_settings;
using ics::handoff::closest_approach;

constexpr float64 launch_speed = 3.0;
constexpr float64 height_m = 30.0;
constexpr float64 gap_m = 100.0;
constexpr float64 target_speed = 5.0;
constexpr float64 interceptor_speed = 15.0;
constexpr float64 slow_speed = 4.0;
constexpr float64 just_inside_m = 239.9;
constexpr float64 just_outside_m = 240.0;
constexpr float64 offset_north_m = 3.0;
constexpr float64 offset_up_m = 4.0;
constexpr float64 offset_m = 5.0;
constexpr float64 near_launch_speed = 2.9;
constexpr float64 tolerance = 1.0E-9;
constexpr ics::Duration horizon = std::chrono::seconds(60);
constexpr ics::Duration meets_after = std::chrono::seconds(5);
constexpr ics::Duration third = std::chrono::nanoseconds(333333333);
constexpr Approach_settings settings{launch_speed, horizon};

const ics::UtcTime now = ics::UtcTime{} + std::chrono::seconds(1);

// An estimate at now, with a position and a velocity.
Estimate estimate(const Vector3& position, const Vector3& velocity)
{
  Estimate made{};
  made.time = now;
  made.state = {position[0], position[1], position[2], velocity[0], velocity[1], velocity[2], 0.0, 0.0, 0.0};
  return made;
}

const Estimate target = estimate({0.0, 0.0, height_m}, {target_speed, 0.0, 0.0});
const Estimate still = estimate({0.0, 0.0, height_m}, {0.0, 0.0, 0.0});

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Approach, AHeadOnInterceptorMeetsItsTargetWhenTheGapIsClosed)
{
  const std::optional<Approach> found =
    closest_approach(target, estimate({gap_m, 0.0, height_m}, {-interceptor_speed, 0.0, 0.0}), settings);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->time, now + meets_after);
  EXPECT_NEAR(found->miss_m, 0.0, tolerance);
}

TEST(Approach, TheMissDistanceIsTheOffsetAcrossTheClosingVelocity)
{
  const Estimate offset = estimate({gap_m, offset_north_m, height_m + offset_up_m}, {-interceptor_speed, 0.0, 0.0});
  const std::optional<Approach> found = closest_approach(still, offset, settings);
  ASSERT_TRUE(found.has_value());
  EXPECT_NEAR(found->miss_m, offset_m, tolerance);
}

TEST(Approach, TheTimeIsRoundedToTheNearestNanosecond)
{
  const std::optional<Approach> found =
    closest_approach(still, estimate({1.0, 0.0, height_m}, {-launch_speed, 0.0, 0.0}), settings);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->time, now + third);
}

TEST(Approach, AnInterceptorThatHasNotLaunchedHasNone)
{
  const Estimate parked = estimate({gap_m, 0.0, 0.0}, {0.0, near_launch_speed, 0.0});
  const Estimate launched = estimate({gap_m, 0.0, 0.0}, {0.0, launch_speed, 0.0});
  EXPECT_FALSE(closest_approach(target, parked, settings).has_value());
  EXPECT_TRUE(closest_approach(target, launched, settings).has_value());
}

TEST(Approach, VehiclesThatAreNotClosingHaveNone)
{
  const Estimate leaving = estimate({gap_m, 0.0, height_m}, {interceptor_speed, 0.0, 0.0});
  const Estimate beside = estimate({gap_m, 0.0, height_m}, {target_speed, 0.0, 0.0});
  EXPECT_FALSE(closest_approach(target, leaving, settings).has_value());
  EXPECT_FALSE(closest_approach(target, beside, settings).has_value());
}

TEST(Approach, AnApproachPastTheHorizonIsNotPredicted)
{
  const Estimate inside = estimate({just_inside_m, 0.0, height_m}, {-slow_speed, 0.0, 0.0});
  const Estimate outside = estimate({just_outside_m, 0.0, height_m}, {-slow_speed, 0.0, 0.0});
  EXPECT_TRUE(closest_approach(still, inside, settings).has_value());
  EXPECT_FALSE(closest_approach(still, outside, settings).has_value());
}

TEST(Approach, EstimatesAtTwoTimesFailACheck)
{
  Estimate late = estimate({gap_m, 0.0, height_m}, {-interceptor_speed, 0.0, 0.0});
  late.time = now + std::chrono::nanoseconds(1);
  EXPECT_FALSE(closest_approach(target, late, settings).has_value());
}

TEST(Approach, SettingsMustBePositive)
{
  EXPECT_TRUE(ics::handoff::valid(settings));
  EXPECT_FALSE(ics::handoff::valid(Approach_settings{0.0, horizon}));
  EXPECT_FALSE(ics::handoff::valid(Approach_settings{std::numeric_limits<float64>::infinity(), horizon}));
  EXPECT_FALSE(ics::handoff::valid(Approach_settings{launch_speed, ics::Duration::zero()}));
}
