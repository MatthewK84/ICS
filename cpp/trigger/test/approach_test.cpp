// approach_test.cpp: closest approaches, coarse and refined (ICS-040).
#include <chrono>
#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>
#include <ics/trigger/approach.h>

namespace
{

using ics::float64;
using ics::cue::Estimate;
using ics::cue::Vector3;
using ics::trigger::Approach;
using ics::trigger::Approach_settings;
using ics::trigger::coarse_approach;
using ics::trigger::refined_approach;

constexpr float64 launch_speed = 3.0;
constexpr float64 height_m = 30.0;
constexpr float64 gap_m = 100.0;
constexpr float64 wide_gap_m = 150.0;
constexpr float64 beside_m = 500.0;
constexpr float64 target_speed = 5.0;
constexpr float64 interceptor_speed = 15.0;
constexpr float64 fast_speed = 20.0;
constexpr float64 slow_speed = 4.0;
constexpr float64 near_launch_speed = 2.9;
constexpr float64 push = 10.0;
constexpr float64 turn = 2.0;
constexpr float64 swing = 0.2;
constexpr float64 drag = 0.051;
constexpr float64 just_inside_m = 239.9;
constexpr float64 just_outside_m = 240.0;
constexpr float64 offset_north_m = 3.0;
constexpr float64 offset_up_m = 4.0;
constexpr float64 offset_m = 5.0;
constexpr float64 turned_miss_m = 22.57;
constexpr float64 sooner_s = 3.58257569495584;
constexpr float64 tolerance = 1.0E-6;
constexpr float64 miss_tolerance = 0.01;
constexpr ics::Duration horizon = std::chrono::seconds(60);
constexpr ics::Duration meets_after = std::chrono::seconds(5);
constexpr ics::Duration swings_past_after = std::chrono::seconds(25);
constexpr ics::Duration drags_in_after = std::chrono::milliseconds(37500);
constexpr Approach_settings settings{launch_speed, horizon};
constexpr Vector3 still{0.0, 0.0, 0.0};

const ics::UtcTime now = ics::UtcTime{} + std::chrono::seconds(1);

// An estimate at now.
Estimate estimate(const Vector3& position,
                  const Vector3& velocity,
                  const Vector3& acceleration)
{
  Estimate made{};
  made.time = now;
  made.state = {position[0],     position[1],     position[2],     velocity[0],    velocity[1],
                velocity[2],     acceleration[0], acceleration[1], acceleration[2]};
  return made;
}

const Estimate target = estimate({0.0, 0.0, height_m}, {target_speed, 0.0, 0.0}, still);
const Estimate hovering = estimate({0.0, 0.0, height_m}, still, still);

float64 seconds_to(const Approach& approach)
{
  return std::chrono::duration<float64>(approach.time - now).count();
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(Approach, AHeadOnInterceptorMeetsItsTargetWhenTheGapIsClosed)
{
  const Estimate head_on = estimate({gap_m, 0.0, height_m}, {-interceptor_speed, 0.0, 0.0}, still);
  const std::optional<Approach> coarse = coarse_approach(target, head_on, settings);
  const std::optional<Approach> refined = refined_approach(target, head_on, settings);
  ASSERT_TRUE(coarse.has_value());
  ASSERT_TRUE(refined.has_value());
  EXPECT_EQ(coarse->time, now + meets_after);
  EXPECT_NEAR(coarse->miss_m, 0.0, tolerance);
  EXPECT_EQ(refined->time, coarse->time);
}

TEST(Approach, TheMissDistanceIsTheOffsetAcrossTheClosingVelocity)
{
  const Estimate offset =
    estimate({gap_m, offset_north_m, height_m + offset_up_m}, {-interceptor_speed, 0.0, 0.0}, still);
  const std::optional<Approach> found = refined_approach(hovering, offset, settings);
  ASSERT_TRUE(found.has_value());
  EXPECT_NEAR(found->miss_m, offset_m, tolerance);
}

TEST(Approach, AnInterceptorThatIsSpeedingUpArrivesSooner)
{
  const Estimate speeding = estimate({gap_m, 0.0, height_m}, {-push, 0.0, 0.0}, {-push, 0.0, 0.0});
  const std::optional<Approach> found = refined_approach(hovering, speeding, settings);
  ASSERT_TRUE(found.has_value());
  EXPECT_NEAR(seconds_to(*found), sooner_s, tolerance);
  EXPECT_NEAR(found->miss_m, 0.0, tolerance);
}

TEST(Approach, ATargetThatTurnsAwayIsMissedByWhatTheTurnOpens)
{
  const Estimate turning = estimate({0.0, 0.0, height_m}, still, {0.0, turn, 0.0});
  const Estimate straight = estimate({gap_m, 0.0, height_m}, {-fast_speed, 0.0, 0.0}, still);
  const std::optional<Approach> found = refined_approach(turning, straight, settings);
  ASSERT_TRUE(found.has_value());
  EXPECT_LT(found->time, now + meets_after);
  EXPECT_NEAR(found->miss_m, turned_miss_m, miss_tolerance);
}

TEST(Approach, StepsThatFindNoMinimumLeaveTheCoarseApproach)
{
  const Estimate swinging = estimate({gap_m, beside_m, height_m}, {-slow_speed, 0.0, 0.0}, {0.0, -swing, 0.0});
  const std::optional<Approach> found = refined_approach(hovering, swinging, settings);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->time, now + swings_past_after);
  EXPECT_NEAR(found->miss_m, beside_m, tolerance);
}

TEST(Approach, StepsThatLeaveTheHorizonLeaveTheCoarseApproach)
{
  const Estimate dragging = estimate({wide_gap_m, 0.0, height_m}, {-slow_speed, 0.0, 0.0}, {drag, 0.0, 0.0});
  const std::optional<Approach> found = refined_approach(hovering, dragging, settings);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->time, now + drags_in_after);
  EXPECT_NEAR(found->miss_m, 0.0, tolerance);
}

TEST(Approach, AnInterceptorThatHasNotLaunchedHasNone)
{
  const Estimate parked = estimate({gap_m, 0.0, 0.0}, {0.0, near_launch_speed, 0.0}, still);
  const Estimate launched = estimate({gap_m, 0.0, 0.0}, {0.0, launch_speed, 0.0}, still);
  EXPECT_FALSE(coarse_approach(target, parked, settings).has_value());
  EXPECT_FALSE(refined_approach(target, parked, settings).has_value());
  EXPECT_TRUE(coarse_approach(target, launched, settings).has_value());
}

TEST(Approach, VehiclesThatAreNotClosingHaveNone)
{
  const Estimate leaving = estimate({gap_m, 0.0, height_m}, {interceptor_speed, 0.0, 0.0}, still);
  EXPECT_FALSE(coarse_approach(target, leaving, settings).has_value());
}

TEST(Approach, AnApproachPastTheHorizonIsNotPredicted)
{
  const Estimate inside = estimate({just_inside_m, 0.0, height_m}, {-slow_speed, 0.0, 0.0}, still);
  const Estimate outside = estimate({just_outside_m, 0.0, height_m}, {-slow_speed, 0.0, 0.0}, still);
  EXPECT_TRUE(coarse_approach(hovering, inside, settings).has_value());
  EXPECT_FALSE(coarse_approach(hovering, outside, settings).has_value());
}

TEST(Approach, EstimatesAtTwoTimesFailACheck)
{
  Estimate late = estimate({gap_m, 0.0, height_m}, {-interceptor_speed, 0.0, 0.0}, still);
  late.time = now + std::chrono::nanoseconds(1);
  EXPECT_FALSE(coarse_approach(target, late, settings).has_value());
}

TEST(Approach, SettingsMustBePositive)
{
  EXPECT_TRUE(ics::trigger::valid(settings));
  EXPECT_FALSE(ics::trigger::valid(Approach_settings{0.0, horizon}));
  EXPECT_FALSE(ics::trigger::valid(Approach_settings{std::numeric_limits<float64>::infinity(), horizon}));
  EXPECT_FALSE(ics::trigger::valid(Approach_settings{launch_speed, ics::Duration::zero()}));
}
