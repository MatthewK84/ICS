// filter_test.cpp: the cue filter on exact truths, late reports, the gate, the history and its refusals (ICS-037).
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/filter.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/measurement.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;
using ics::cue::Cue_filter;
using ics::cue::Measurement;
using ics::cue::Outcome;
using ics::cue::Vector3;

constexpr int64 period_ns = 100000000;
constexpr int32 reports = 60;
constexpr float64 sigma_m = 0.5;
constexpr float64 velocity_sigma_m_s = 0.2;
constexpr float64 exact_m = 1.0E-3;
constexpr float64 same = 1.0E-12;
constexpr float64 outlier_m = 40.0;
constexpr int64 seed = 37;
constexpr ics::cue::Filter_settings settings{{4.0, 1.0},
                                             16.27,
                                             std::chrono::seconds(2),
                                             std::chrono::seconds(2),
                                             ics::cue::max_history,
                                             20.0,
                                             10.0};

ics::UtcTime at_ns(const int64 nanoseconds)
{
  return ics::UtcTime{} + ics::Duration(nanoseconds);
}

// A constant-acceleration target's position and velocity at a time.
struct Truth
{
  Vector3 position;
  Vector3 velocity;
};

Truth truth(const int64 nanoseconds)
{
  const float64 seconds = static_cast<float64>(nanoseconds) / 1.0E9;
  const Vector3 start{-200.0, 10.0, 35.0};
  const Vector3 velocity{5.0, -1.0, 0.5};
  const Vector3 acceleration{0.8, 0.4, -0.1};
  Truth now{};
  for (int32 axis = 0; axis < ics::cue::axes; ++axis)
  {
    const std::size_t place = static_cast<std::size_t>(axis);
    now.position[place] = start[place] + (velocity[place] * seconds) + ((acceleration[place] * seconds * seconds) / 2.0);
    now.velocity[place] = velocity[place] + (acceleration[place] * seconds);
  }
  return now;
}

Measurement report(const int64 nanoseconds,
                   const Vector3& position,
                   const bool moving,
                   const Vector3& velocity)
{
  return {at_ns(nanoseconds), position, sigma_m, sigma_m, moving, velocity, velocity_sigma_m_s};
}

// Reports of the target every 100 ms, with noise when noisy.
std::vector<Measurement> flown(const int32 count, const bool noisy)
{
  std::mt19937_64 generator(seed);
  std::normal_distribution<float64> noise(0.0, sigma_m);
  std::vector<Measurement> flight;
  for (int32 number = 0; number < count; ++number)
  {
    const int64 time = number * period_ns;
    Truth now = truth(time);
    for (float64& value : now.position)
    {
      value += noisy ? noise(generator) : 0.0;
    }
    flight.push_back(report(time, now.position, true, now.velocity));
  }
  return flight;
}

// The report at a place in a flight.
const Measurement& nth(const std::vector<Measurement>& flight, const int32 number)
{
  return flight[static_cast<std::size_t>(number)];
}

// How many reports a flight holds.
int32 length(const std::vector<Measurement>& flight)
{
  return static_cast<int32>(flight.size());
}

Cue_filter filled(const std::vector<Measurement>& flight)
{
  Cue_filter filter(settings);
  for (const Measurement& measured : flight)
  {
    EXPECT_NE(filter.add(measured), Outcome::too_old);
  }
  return filter;
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(CueFilter, FollowsAConstantAccelerationExactly)
{
  const Cue_filter filter = filled(flown(reports, false));
  const int64 ahead = ((reports - 1) * period_ns) + (3 * period_ns);
  const ics::Result<ics::cue::Estimate> estimate = filter.estimate_at(at_ns(ahead));
  ASSERT_TRUE(estimate.has_value());
  const Truth expected = truth(ahead);
  for (int32 axis = 0; axis < ics::cue::axes; ++axis)
  {
    const std::size_t place = static_cast<std::size_t>(axis);
    EXPECT_NEAR(ics::cue::component(estimate->state, axis), expected.position[place], exact_m);
    EXPECT_NEAR(ics::cue::component(estimate->state, ics::cue::index(1, axis)), expected.velocity[place], exact_m);
  }
}

TEST(CueFilter, LateReportsGiveTheInOrderResult)
{
  const std::vector<Measurement> flight = flown(reports, true);
  const Cue_filter in_order = filled(flight);
  std::vector<Measurement> arrival = flight;
  for (int32 number = 4; (number + 3) < length(arrival); number += 5)
  {
    std::swap(arrival[static_cast<std::size_t>(number)], arrival[static_cast<std::size_t>(number + 3)]);
  }
  Cue_filter shuffled(settings);
  int32 late = 0;
  for (const Measurement& measured : arrival)
  {
    late += (shuffled.add(measured) == Outcome::late) ? 1 : 0;
  }
  EXPECT_GT(late, 0);
  const ics::UtcTime last = flight.back().time;
  const ics::Result<ics::cue::Estimate> expected = in_order.estimate_at(last);
  const ics::Result<ics::cue::Estimate> actual = shuffled.estimate_at(last);
  ASSERT_TRUE(expected.has_value());
  ASSERT_TRUE(actual.has_value());
  for (int32 position = 0; position < ics::cue::state_size; ++position)
  {
    EXPECT_NEAR(ics::cue::component(actual->state, position), ics::cue::component(expected->state, position), same);
  }
}

TEST(CueFilter, AReportBeforeTheFirstRefiltersFromIt)
{
  const std::vector<Measurement> flight = flown(3, false);
  Cue_filter filter(settings);
  EXPECT_EQ(filter.add(nth(flight, 1)), Outcome::taken);
  EXPECT_EQ(filter.add(nth(flight, 0)), Outcome::late);
  EXPECT_EQ(filter.add(nth(flight, 2)), Outcome::taken);
  const Cue_filter straight = filled(flight);
  const ics::Result<ics::cue::Estimate> expected = straight.estimate_at(nth(flight, 2).time);
  const ics::Result<ics::cue::Estimate> actual = filter.estimate_at(nth(flight, 2).time);
  ASSERT_TRUE(expected.has_value());
  ASSERT_TRUE(actual.has_value());
  EXPECT_NEAR(ics::cue::component(actual->state, 0), ics::cue::component(expected->state, 0), same);
}

TEST(CueFilter, GatesAnOutlierAndKeepsGoing)
{
  const std::vector<Measurement> flight = flown(20, true);
  Cue_filter filter(settings);
  for (int32 number = 0; number < 10; ++number)
  {
    EXPECT_EQ(filter.add(nth(flight, number)), Outcome::taken);
  }
  Measurement outlier = nth(flight, 10);
  outlier.position[0] += outlier_m;
  EXPECT_EQ(filter.add(outlier), Outcome::gated);
  Measurement unweighable = nth(flight, 11);
  unweighable.horizontal_sigma_m = std::numeric_limits<float64>::quiet_NaN();
  EXPECT_EQ(filter.add(unweighable), Outcome::gated);
  Measurement unsteady = nth(flight, 11);
  unsteady.velocity_sigma_m_s = std::numeric_limits<float64>::quiet_NaN();
  EXPECT_EQ(filter.add(unsteady), Outcome::gated);
  for (int32 number = 12; number < length(flight); ++number)
  {
    EXPECT_EQ(filter.add(nth(flight, number)), Outcome::taken);
  }
}

TEST(CueFilter, RefusesReportsOlderThanTheHistory)
{
  const std::vector<Measurement> flight = flown(40, false);
  Cue_filter filter(settings);
  for (int32 number = 10; number < length(flight); ++number)
  {
    EXPECT_EQ(filter.add(nth(flight, number)), Outcome::taken);
  }
  EXPECT_EQ(filter.add(nth(flight, 0)), Outcome::too_old);
  EXPECT_EQ(filter.add(nth(flight, 10)), Outcome::too_old);
}

TEST(CueFilter, EstimatesOnlyWithinTheHistoryAndTheCoastLimit)
{
  Cue_filter empty(settings);
  EXPECT_EQ(empty.estimate_at(at_ns(0)).error(), ics::Error::kOutOfRange);
  EXPECT_EQ(empty.latest().error(), ics::Error::kEmpty);
  const Cue_filter filter = filled(flown(40, false));
  const int64 last = 39 * period_ns;
  const int64 coast = std::chrono::duration_cast<ics::Duration>(settings.coast_limit).count();
  EXPECT_TRUE(filter.estimate_at(at_ns(last + coast)).has_value());
  EXPECT_EQ(filter.estimate_at(at_ns(last + coast + 1)).error(), ics::Error::kUnconstrained);
  const int64 held_from = last - std::chrono::duration_cast<ics::Duration>(settings.history).count();
  EXPECT_TRUE(filter.estimate_at(at_ns(held_from - 1)).has_value());
  EXPECT_EQ(filter.estimate_at(at_ns(0)).error(), ics::Error::kOutOfRange);
}

TEST(CueFilter, TheCapacityBoundsTheHistory)
{
  ics::cue::Filter_settings small = settings;
  small.capacity = 4;
  Cue_filter filter(small);
  const std::vector<Measurement> flight = flown(10, false);
  for (const Measurement& measured : flight)
  {
    EXPECT_EQ(filter.add(measured), Outcome::taken);
  }
  // The fifth report was dropped for room, so its checkpoint is the base: an earlier report is too old.
  EXPECT_EQ(filter.add(nth(flight, 4)), Outcome::too_old);
  EXPECT_EQ(filter.add(nth(flight, 5)), Outcome::late);
}

TEST(CueFilter, ClampsACapacityOutOfRange)
{
  ics::cue::Filter_settings none = settings;
  none.capacity = 0;
  Cue_filter tiny(none);
  const std::vector<Measurement> flight = flown(3, false);
  EXPECT_EQ(tiny.add(nth(flight, 1)), Outcome::taken);
  EXPECT_EQ(tiny.add(nth(flight, 2)), Outcome::taken);
  EXPECT_EQ(tiny.add(nth(flight, 0)), Outcome::too_old);
  ics::cue::Filter_settings many = settings;
  many.capacity = ics::cue::max_history + 1;
  Cue_filter full(many);
  for (const Measurement& measured : flown(reports, false))
  {
    EXPECT_EQ(full.add(measured), Outcome::taken);
  }
}

TEST(CueFilter, PositionOnlyReportsStartStill)
{
  Cue_filter filter(settings);
  EXPECT_EQ(filter.add(report(0, {1.0, 2.0, 3.0}, false, {})), Outcome::taken);
  const ics::Result<ics::cue::Estimate> start = filter.estimate_at(at_ns(0));
  ASSERT_TRUE(start.has_value());
  EXPECT_DOUBLE_EQ(ics::cue::component(start->state, ics::cue::index(1, 0)), 0.0);
  EXPECT_DOUBLE_EQ(start->covariance.at(ics::cue::index(1, 0), ics::cue::index(1, 0)), 400.0);
  EXPECT_EQ(filter.add(report(period_ns, {1.5, 2.0, 3.0}, false, {})), Outcome::taken);
  const ics::Result<ics::cue::Estimate> moved = filter.estimate_at(at_ns(period_ns));
  ASSERT_TRUE(moved.has_value());
  EXPECT_GT(ics::cue::component(moved->state, ics::cue::index(1, 0)), 0.0);
}
