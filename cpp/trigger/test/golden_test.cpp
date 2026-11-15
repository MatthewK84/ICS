// golden_test.cpp: ics::trigger against golden/trigger, which the Python reference writes (ICS-040).
// Every flag, edge and delay must match, every approach's time within 2 ns,
// and every miss within 1e-9 of the larger of it and 1.
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
#include <ics/trigger/approach.h>
#include <ics/trigger/shot.h>

namespace
{

using ics::float64;
using ics::int64;
using ics::tracker_test::field_of;
using ics::tracker_test::Golden_table;
using ics::tracker_test::integer_of;
using ics::tracker_test::number_of;
using Row = std::vector<std::string>;

constexpr float64 relative = 1.0E-9;
constexpr int64 time_tolerance_ns = 2;
constexpr std::size_t least_rows = 50U;

Golden_table table(const std::string& name)
{
  return ics::tracker_test::read_table(std::string(ICS_GOLDEN_TRIGGER_DIR) + "/" + name);
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
                    number(golden, row, prefix + "_a_east"),
                    number(golden, row, prefix + "_a_north"),
                    number(golden, row, prefix + "_a_up")};
  return estimate;
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(TriggerGolden, RefinedApproachesMatchTheReference)
{
  const Golden_table parameters = table("parameters.csv");
  const ics::trigger::Approach_settings settings{parameter(parameters, "launch_speed_m_s"),
                                                 span(parameters, "horizon_ns")};
  const Golden_table golden = table("approaches.csv");
  ASSERT_GE(golden.rows.size(), least_rows);
  int64 found = 0;
  for (const Row& row : golden.rows)
  {
    const std::optional<ics::trigger::Approach> approach = ics::trigger::refined_approach(
      estimate_of(golden, row, "target"), estimate_of(golden, row, "interceptor"), settings);
    const std::string what = "at row " + field_of(golden, row, "target_east");
    ASSERT_EQ(approach.has_value(), whole(golden, row, "found") == 1) << what;
    if (approach.has_value())
    {
      ++found;
      EXPECT_LE(std::chrono::abs(approach->time - time_of(golden, row, "approach_time_ns")).count(),
                time_tolerance_ns)
        << what;
      const float64 miss_m = number(golden, row, "miss_m");
      EXPECT_NEAR(approach->miss_m, miss_m, relative * std::max(1.0, std::abs(miss_m))) << what;
    }
  }
  EXPECT_GT(found, 0);
}

TEST(TriggerGolden, ShotsMatchTheReference)
{
  const Golden_table parameters = table("parameters.csv");
  const ics::trigger::Shot_settings settings{span(parameters, "period_ns"),
                                             span(parameters, "offset_ns"),
                                             span(parameters, "command_margin_ns"),
                                             span(parameters, "guard_ns")};
  ASSERT_TRUE(ics::trigger::valid(settings));
  const Golden_table golden = table("shots.csv");
  ASSERT_GE(golden.rows.size(), least_rows);
  for (const Row& row : golden.rows)
  {
    const ics::UtcTime now = time_of(golden, row, "now_ns");
    const ics::trigger::Shot shot = ics::trigger::shot_for(time_of(golden, row, "fire_ns"), now, settings);
    const std::string what = "for a pulse at " + field_of(golden, row, "fire_ns");
    EXPECT_EQ(shot.edge, time_of(golden, row, "edge_ns")) << what;
    EXPECT_EQ(shot.delay.count(), whole(golden, row, "delay_ns")) << what;
    EXPECT_EQ(shot.late, whole(golden, row, "late") == 1) << what;
    EXPECT_EQ(ics::trigger::window_open(shot, now, settings), whole(golden, row, "open") == 1) << what;
  }
}
