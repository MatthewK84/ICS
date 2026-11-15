// golden_test.cpp: ics::handoff against golden/handoff, which the Python reference writes (ICS-039).
// Every flag, place and engagement served must match, every approach's time
// within 2 ns, and every other number within 1e-9 of the larger of it and 1.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <golden_table.h>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/handoff/approach.h>
#include <ics/handoff/queue.h>
#include <ics/handoff/slew.h>
#include <ics/tracker/demand.h>
#include <ics/tracker/estimator.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;
using ics::handoff::Approach;
using ics::tracker_test::field_of;
using ics::tracker_test::Golden_table;
using ics::tracker_test::integer_of;
using ics::tracker_test::number_of;
using Row = std::vector<std::string>;

constexpr float64 relative = 1.0E-9;
constexpr int64 time_tolerance_ns = 2;

Golden_table table(const std::string& name)
{
  return ics::tracker_test::read_table(std::string(ICS_GOLDEN_HANDOFF_DIR) + "/" + name);
}

void expect_close(const float64 actual,
                  const float64 expected,
                  const std::string& what)
{
  EXPECT_NEAR(actual, expected, relative * std::max(1.0, std::abs(expected))) << what;
}

float64 number(const Golden_table& golden,
               const Row& row,
               const std::string& name)
{
  return number_of(field_of(golden, row, name));
}

int64 whole(const Golden_table& golden,
            const Row& row,
            const std::string& name)
{
  return integer_of(field_of(golden, row, name));
}

ics::UtcTime time_of(const Golden_table& golden,
                     const Row& row,
                     const std::string& name)
{
  return ics::UtcTime{} + ics::Duration(whole(golden, row, name));
}

void expect_same_time(const ics::UtcTime actual,
                      const ics::UtcTime expected,
                      const std::string& what)
{
  EXPECT_LE(std::chrono::abs(actual - expected).count(), time_tolerance_ns) << what;
}

const Row& row_at(const Golden_table& golden, const int64 number)
{
  return golden.rows[static_cast<std::size_t>(number)];
}

// The value of a name in parameters.csv.
float64 parameter(const Golden_table& parameters, const std::string& name)
{
  float64 value = 0.0;
  for (const Row& row : parameters.rows)
  {
    if (field_of(parameters, row, "name") == name)
    {
      value = number(parameters, row, "value");
    }
  }
  return value;
}

ics::Duration span(const Golden_table& parameters, const std::string& name)
{
  return ics::Duration(std::llround(parameter(parameters, name)));
}

ics::handoff::Slew_limits limits_of(const Golden_table& parameters)
{
  return {parameter(parameters, "rate_rad_s"),
          parameter(parameters, "acceleration_rad_s2"),
          parameter(parameters, "gain")};
}

// An estimate from a row's columns under a prefix.
ics::cue::Estimate estimate_of(const Golden_table& golden,
                               const Row& row,
                               const std::string& prefix)
{
  ics::cue::Estimate estimate{};
  estimate.time = time_of(golden, row, "time_ns");
  estimate.state = {number(golden, row, prefix + "_east"),
                    number(golden, row, prefix + "_north"),
                    number(golden, row, prefix + "_up"),
                    number(golden, row, prefix + "_v_east"),
                    number(golden, row, prefix + "_v_north"),
                    number(golden, row, prefix + "_v_up"),
                    0.0,
                    0.0,
                    0.0};
  return estimate;
}

ics::tracker::Angle_state state_of(const Golden_table& golden,
                                   const Row& row,
                                   const std::string& prefix)
{
  return {time_of(golden, row, prefix + "_time_ns"),
          number(golden, row, prefix + "_azimuth"),
          number(golden, row, prefix + "_elevation"),
          number(golden, row, prefix + "_azimuth_rate"),
          number(golden, row, prefix + "_elevation_rate")};
}

// The sighting a row of queue.csv gives.
ics::handoff::Sighting sighting_of(const Golden_table& golden, const Row& row)
{
  ics::handoff::Sighting sighting{};
  if (whole(golden, row, "seen") == 1)
  {
    sighting.approach = Approach{time_of(golden, row, "seen_time_ns"), number(golden, row, "seen_miss_m")};
  }
  if (whole(golden, row, "sighted") == 1)
  {
    sighting.direction = ics::encoder::Direction{number(golden, row, "azimuth"), number(golden, row, "elevation")};
  }
  return sighting;
}

// An engagement's place in a plan's order; -1 when it is not in it.
int64 place_of(const ics::handoff::Plan& plan, const int64 engagement)
{
  int64 place = -1;
  for (int32 position = 0; position < plan.waiting; ++position)
  {
    if (plan.order[static_cast<std::size_t>(position)] == engagement)
    {
      place = position;
    }
  }
  return place;
}

// Checks an engagement's slot, and the plan, against a row of queue.csv.
void expect_row(const ics::handoff::Plan& plan,
                const Golden_table& golden,
                const Row& row)
{
  const int64 engagement = whole(golden, row, "engagement");
  const std::string what = "step " + field_of(golden, row, "step") + " engagement " + std::to_string(engagement);
  const ics::handoff::Slot& slot = plan.slots[static_cast<std::size_t>(engagement)];
  EXPECT_EQ(slot.done, whole(golden, row, "done") == 1) << what;
  EXPECT_EQ(slot.timed, whole(golden, row, "held") == 1) << what;
  if (slot.timed)
  {
    EXPECT_EQ(slot.approach.time, time_of(golden, row, "held_time_ns")) << what;
    expect_close(slot.approach.miss_m, number(golden, row, "held_miss_m"), what);
  }
  EXPECT_EQ(slot.too_close, whole(golden, row, "too_close") == 1) << what;
  EXPECT_EQ(slot.out_of_reach, whole(golden, row, "out_of_reach") == 1) << what;
  EXPECT_EQ(place_of(plan, engagement), whole(golden, row, "place")) << what;
  EXPECT_EQ(static_cast<int64>(plan.current.value_or(-1)), whole(golden, row, "current")) << what;
  EXPECT_EQ(plan.dwelling, whole(golden, row, "dwelling") == 1) << what;
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(HandoffGolden, ApproachesMatchTheReference)
{
  const Golden_table parameters = table("parameters.csv");
  const ics::handoff::Approach_settings settings{parameter(parameters, "launch_speed_m_s"),
                                                 span(parameters, "horizon_ns")};
  const Golden_table golden = table("approaches.csv");
  ASSERT_FALSE(golden.rows.empty());
  int32 found = 0;
  for (const Row& row : golden.rows)
  {
    const std::optional<Approach> approach = ics::handoff::closest_approach(
      estimate_of(golden, row, "target"), estimate_of(golden, row, "interceptor"), settings);
    ASSERT_EQ(approach.has_value(), whole(golden, row, "found") == 1);
    if (approach.has_value())
    {
      expect_same_time(approach->time, time_of(golden, row, "approach_time_ns"), "time");
      expect_close(approach->miss_m, number(golden, row, "miss_m"), "miss_m");
      ++found;
    }
  }
  EXPECT_GT(found, 0);
  EXPECT_LT(static_cast<std::size_t>(found), golden.rows.size());
}

TEST(HandoffGolden, SlewsMatchTheReference)
{
  const ics::handoff::Slew_limits limits = limits_of(table("parameters.csv"));
  const Golden_table golden = table("slews.csv");
  ASSERT_FALSE(golden.rows.empty());
  for (const Row& row : golden.rows)
  {
    const ics::tracker::Axis_rates rates =
      ics::handoff::slew_demand(state_of(golden, row, "target"), state_of(golden, row, "mount"), limits);
    expect_close(rates.azimuth_rad_s, number(golden, row, "azimuth_rate"), "azimuth_rate");
    expect_close(rates.elevation_rad_s, number(golden, row, "elevation_rate"), "elevation_rate");
  }
}

TEST(HandoffGolden, ReachesMatchTheReference)
{
  const ics::handoff::Slew_limits limits = limits_of(table("parameters.csv"));
  const Golden_table golden = table("reaches.csv");
  ASSERT_FALSE(golden.rows.empty());
  for (const Row& row : golden.rows)
  {
    const ics::encoder::Direction first{number(golden, row, "first_azimuth"), number(golden, row, "first_elevation")};
    const ics::encoder::Direction second{number(golden, row, "second_azimuth"),
                                         number(golden, row, "second_elevation")};
    expect_close(ics::handoff::reach_seconds(first, second, limits), number(golden, row, "seconds"), "seconds");
  }
}

TEST(HandoffGolden, TheQueueMatchesTheReference)
{
  const Golden_table parameters = table("parameters.csv");
  const ics::handoff::Queue_settings settings{span(parameters, "min_gap_ns"),
                                              span(parameters, "dwell_ns"),
                                              span(parameters, "margin_ns"),
                                              span(parameters, "settle_ns"),
                                              parameter(parameters, "engaged_miss_m")};
  const int64 count = std::llround(parameter(parameters, "engagements"));
  ics::handoff::Queue queue(static_cast<int32>(count), settings, limits_of(parameters));
  const Golden_table golden = table("queue.csv");
  const int64 rows = static_cast<int64>(golden.rows.size());
  ASSERT_GT(rows, 0);
  ASSERT_EQ(rows % count, 0);
  for (int64 first = 0; first < rows; first += count)
  {
    ics::handoff::Sightings sightings{};
    for (int64 engagement = 0; engagement < count; ++engagement)
    {
      sightings[static_cast<std::size_t>(engagement)] = sighting_of(golden, row_at(golden, first + engagement));
    }
    const ics::handoff::Plan plan = queue.update(time_of(golden, row_at(golden, first), "now_ns"), sightings);
    for (int64 engagement = 0; engagement < count; ++engagement)
    {
      expect_row(plan, golden, row_at(golden, first + engagement));
    }
  }
}
