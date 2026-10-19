// estimator_test.cpp: the alpha-beta track and the demand (ICS-038).
#include <chrono>
#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/tracker/demand.h>
#include <ics/tracker/estimator.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::tracker::Angle_state;

constexpr ics::Duration step = std::chrono::milliseconds(10);
constexpr int32 steps = 400;
constexpr float64 azimuth_rate = 0.1;
constexpr float64 elevation_rate = -0.02;
constexpr float64 relative = 1.0E-6;
constexpr float64 tight = 1.0E-12;
constexpr float64 alpha = 0.5;
constexpr float64 beta = 0.1;
constexpr float64 full_turn = 2.0 * std::numbers::pi;
constexpr float64 across_north = 0.001;
constexpr float64 elevation = 0.5;
constexpr float64 gain = 2.0;
constexpr float64 error_rad = 0.01;
constexpr ics::Duration lead = std::chrono::milliseconds(100);
constexpr float64 lead_s = 0.1;
constexpr float64 unstable_beta = 3.0;
constexpr ics::Duration half_second = std::chrono::milliseconds(500);

const ics::UtcTime start = ics::UtcTime{} + std::chrono::seconds(1);

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(TrackerEstimator, LearnsASteadyTargetsRate)
{
  ics::tracker::Angle_estimator track({alpha, beta});
  track.start(Angle_state{start, 1.0, elevation, 0.0, 0.0});
  ics::Result<Angle_state> state = track.state();
  for (int32 count = 1; count < steps; ++count)
  {
    const float64 seconds = static_cast<float64>(count) * std::chrono::duration<float64>(step).count();
    state = track.update(start + (count * step),
                         {1.0 + (azimuth_rate * seconds), elevation + (elevation_rate * seconds)});
  }
  ASSERT_TRUE(state.has_value());
  EXPECT_NEAR(state->azimuth_rate_rad_s, azimuth_rate, azimuth_rate * relative);
  EXPECT_NEAR(state->elevation_rate_rad_s, elevation_rate, std::abs(elevation_rate) * relative);
  const Angle_state ahead = ics::tracker::carried(*state, state->time + half_second);
  EXPECT_NEAR(ahead.azimuth_rad - state->azimuth_rad, azimuth_rate / 2.0, tight);
}

TEST(TrackerEstimator, GoesTheShortWayAcrossNorth)
{
  ics::tracker::Angle_estimator track({1.0, 0.0});
  track.start(Angle_state{start, full_turn - across_north, elevation, 0.0, 0.0});
  const ics::Result<Angle_state> state = track.update(start + step, {across_north, elevation});
  ASSERT_TRUE(state.has_value());
  EXPECT_NEAR(state->azimuth_rad, across_north, tight);
}

TEST(TrackerEstimator, RefusesWhatItCannotTake)
{
  ics::tracker::Angle_estimator track({alpha, beta});
  EXPECT_EQ(track.state().error(), ics::Error::kEmpty);
  EXPECT_EQ(track.update(start, {1.0, elevation}).error(), ics::Error::kEmpty);
  track.start(Angle_state{start, 1.0, elevation, 0.0, 0.0});
  EXPECT_EQ(track.update(start, {1.0, elevation}).error(), ics::Error::kOutOfRange);
}

TEST(TrackerEstimator, KnowsStableGains)
{
  EXPECT_TRUE(ics::tracker::stable({alpha, beta}));
  EXPECT_FALSE(ics::tracker::stable({0.0, beta}));
  EXPECT_FALSE(ics::tracker::stable({1.0 + alpha, beta}));
  EXPECT_FALSE(ics::tracker::stable({alpha, -beta}));
  EXPECT_FALSE(ics::tracker::stable({alpha, unstable_beta}));
  const ics::tracker::Angle_estimator refused({0.0, beta});
  EXPECT_EQ(refused.state().error(), ics::Error::kEmpty);
}

TEST(TrackerDemand, FollowsTheTargetAndClosesTheError)
{
  const Angle_state mount{start, 1.0, elevation, azimuth_rate, 0.0};
  const Angle_state target{
    start + lead, 1.0 + (azimuth_rate * lead_s) + error_rad, elevation - error_rad, azimuth_rate, elevation_rate};
  const ics::tracker::Axis_rates rates = ics::tracker::demand(target, mount, gain);
  EXPECT_NEAR(rates.azimuth_rad_s, azimuth_rate + (gain * error_rad), tight);
  EXPECT_NEAR(rates.elevation_rad_s, elevation_rate - (gain * error_rad), tight);
}

TEST(TrackerDemand, GoesTheShortWayAndSteersWithNoGainForANegativeOne)
{
  const Angle_state mount{start, full_turn - error_rad, elevation, 0.0, 0.0};
  const Angle_state target{start, error_rad, elevation, azimuth_rate, 0.0};
  EXPECT_NEAR(ics::tracker::demand(target, mount, 1.0).azimuth_rad_s, azimuth_rate + (2.0 * error_rad), tight);
  EXPECT_NEAR(ics::tracker::demand(target, mount, -1.0).azimuth_rad_s, azimuth_rate, tight);
}
