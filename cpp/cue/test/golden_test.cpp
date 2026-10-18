// golden_test.cpp: ics::cue against golden/cue, which the Python reference writes (ICS-037).
// Every outcome and refusal must match, every state and covariance value within
// 1e-9 of the larger of it and 1, and every angle within 1e-10 rad: the C++
// frames module and the reference put a station in the range frame by
// different arithmetic, about 1e-9 m apart.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <golden_table.h>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/filter.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/measurement.h>
#include <ics/cue/pointing.h>
#include <ics/frames/enu.hpp>
#include <ics/frames/geodetic.hpp>
#include <ics/v1/pli.pb.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::int64;
using ics::cue::Estimate;
using ics::cue_test::Golden_table;
using ics::cue_test::field_of;
using ics::cue_test::integer_of;
using ics::cue_test::number_of;
using Row = std::vector<std::string>;

constexpr float64 relative = 1.0E-9;
constexpr float64 angle_rad = 1.0E-10;
// The C++ frames module and the Python reference compute ENU positions in different ways.
constexpr float64 position_m = 1.0E-6;
constexpr float64 velocity_m_s = 1.0E-11;

Golden_table table(const std::string& name)
{
  return ics::cue_test::read_table(std::string(ICS_GOLDEN_CUE_DIR) + "/" + name);
}

float64 parameter(const Golden_table& parameters, const std::string& name)
{
  float64 value = 0.0;
  for (const Row& row : parameters.rows)
  {
    if (field_of(parameters, row, "name") == name)
    {
      value = number_of(field_of(parameters, row, "value"));
    }
  }
  return value;
}

ics::Duration nanoseconds(const float64 value)
{
  return ics::Duration(static_cast<int64>(value));
}

ics::cue::Filter_settings settings_of(const Golden_table& parameters)
{
  return {{parameter(parameters, "horizontal_jerk_psd"), parameter(parameters, "vertical_jerk_psd")},
          parameter(parameters, "gate"),
          nanoseconds(parameter(parameters, "history_ns")),
          nanoseconds(parameter(parameters, "coast_limit_ns")),
          static_cast<int32>(parameter(parameters, "capacity")),
          parameter(parameters, "initial_velocity_sigma_m_s"),
          parameter(parameters, "initial_acceleration_sigma_m_s2")};
}

ics::frames::EnuFrame range_of(const Golden_table& parameters)
{
  const ics::Result<ics::frames::Geodetic> origin =
    ics::frames::Geodetic::make(ics::Degrees(parameter(parameters, "origin_latitude_deg")),
                                ics::Degrees(parameter(parameters, "origin_longitude_deg")),
                                ics::Meters(parameter(parameters, "origin_height_m")));
  EXPECT_TRUE(origin.has_value());
  return ics::frames::EnuFrame(*origin);
}

ics::UtcTime time_of(const std::string& field)
{
  return ics::UtcTime{} + ics::Duration(integer_of(field));
}

ics::cue::Vector3 triple(const Golden_table& golden,
                         const Row& row,
                         const std::string& east,
                         const std::string& north,
                         const std::string& up)
{
  return {number_of(field_of(golden, row, east)),
          number_of(field_of(golden, row, north)),
          number_of(field_of(golden, row, up))};
}

ics::cue::Measurement report_of(const Golden_table& reports, const Row& row)
{
  ics::cue::Measurement report{};
  report.time = time_of(field_of(reports, row, "time_ns"));
  report.position = triple(reports, row, "east_m", "north_m", "up_m");
  report.horizontal_sigma_m = number_of(field_of(reports, row, "horizontal_sigma_m"));
  report.vertical_sigma_m = number_of(field_of(reports, row, "vertical_sigma_m"));
  report.has_velocity = integer_of(field_of(reports, row, "has_velocity")) == 1;
  report.velocity = triple(reports, row, "velocity_east_m_s", "velocity_north_m_s", "velocity_up_m_s");
  report.velocity_sigma_m_s = number_of(field_of(reports, row, "velocity_sigma_m_s"));
  return report;
}

std::string name_of(const ics::cue::Outcome outcome)
{
  std::string name = "too_old";
  if (outcome == ics::cue::Outcome::taken)
  {
    name = "taken";
  }
  else if (outcome == ics::cue::Outcome::late)
  {
    name = "late";
  }
  else if (outcome == ics::cue::Outcome::gated)
  {
    name = "gated";
  }
  else
  {
    name = "too_old";
  }
  return name;
}

void expect_close(const float64 actual, const float64 expected, const std::string& what)
{
  EXPECT_NEAR(actual, expected, relative * std::max(std::fabs(expected), 1.0)) << what;
}

void expect_estimate(const Estimate& estimate,
                     const Golden_table& golden,
                     const Row& row)
{
  for (int32 position = 0; position < ics::cue::state_size; ++position)
  {
    const std::string name = "x" + std::to_string(position);
    expect_close(ics::cue::component(estimate.state, position), number_of(field_of(golden, row, name)), name);
    for (int32 column = position; column < ics::cue::state_size; ++column)
    {
      const std::string cell = "p" + std::to_string(position) + std::to_string(column);
      expect_close(estimate.covariance.at(position, column), number_of(field_of(golden, row, cell)), cell);
    }
  }
}

ics::v1::PliRecord record_of(const Golden_table& golden, const Row& row)
{
  ics::v1::PliRecord record;
  record.set_valid_utc_ns(1);
  record.set_fix_type(ics::v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  record.mutable_position()->set_latitude_deg(number_of(field_of(golden, row, "latitude_deg")));
  record.mutable_position()->set_longitude_deg(number_of(field_of(golden, row, "longitude_deg")));
  record.mutable_position()->set_height_ellipsoid_m(number_of(field_of(golden, row, "height_m")));
  record.mutable_velocity_enu_mps()->set_east(number_of(field_of(golden, row, "velocity_east_m_s")));
  record.mutable_velocity_enu_mps()->set_north(number_of(field_of(golden, row, "velocity_north_m_s")));
  record.mutable_velocity_enu_mps()->set_up(number_of(field_of(golden, row, "velocity_up_m_s")));
  return record;
}

Estimate cue_estimate(const Golden_table& golden, const Row& row)
{
  Estimate estimate{time_of(field_of(golden, row, "time_ns")), {}, ics::cue::Matrix{}};
  for (int32 position = 0; position < (2 * ics::cue::axes); ++position)
  {
    ics::cue::set_component(estimate.state, position, number_of(field_of(golden, row, "x" + std::to_string(position))));
  }
  for (int32 first = 0; first < ics::cue::axes; ++first)
  {
    for (int32 second = first; second < ics::cue::axes; ++second)
    {
      const float64 value = number_of(field_of(golden, row, "p" + std::to_string(first) + std::to_string(second)));
      estimate.covariance.put(first, second, value);
      estimate.covariance.put(second, first, value);
    }
  }
  return estimate;
}

void expect_cue(const ics::Result<ics::cue::Cue>& cue,
                const Golden_table& golden,
                const Row& row)
{
  const bool ok = integer_of(field_of(golden, row, "ok")) == 1;
  ASSERT_EQ(cue.has_value(), ok);
  if (ok)
  {
    EXPECT_NEAR(cue->azimuth_rad, number_of(field_of(golden, row, "azimuth_rad")), angle_rad);
    EXPECT_NEAR(cue->elevation_rad, number_of(field_of(golden, row, "elevation_rad")), angle_rad);
    expect_close(cue->azimuth_rate_rad_s, number_of(field_of(golden, row, "azimuth_rate_rad_s")), "az rate");
    expect_close(cue->elevation_rate_rad_s, number_of(field_of(golden, row, "elevation_rate_rad_s")), "el rate");
    expect_close(cue->range_m, number_of(field_of(golden, row, "range_m")), "range");
    expect_close(cue->sigma_rad, number_of(field_of(golden, row, "sigma_rad")), "sigma");
  }
  else
  {
    EXPECT_EQ(cue.error(), ics::Error::kUnconstrained);
  }
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(CueGolden, FiltersEveryReportAsTheReferenceDoes)
{
  const Golden_table parameters = table("parameters.csv");
  const Golden_table reports = table("reports.csv");
  ASSERT_FALSE(reports.rows.empty());
  ics::cue::Cue_filter filter(settings_of(parameters));
  for (const Row& row : reports.rows)
  {
    const ics::cue::Outcome outcome = filter.add(report_of(reports, row));
    EXPECT_EQ(name_of(outcome), field_of(reports, row, "outcome")) << field_of(reports, row, "time_ns");
    const ics::Result<ics::UtcTime> latest = filter.latest();
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(*latest, time_of(field_of(reports, row, "latest_ns")));
    const ics::Result<Estimate> estimate = filter.estimate_at(*latest);
    ASSERT_TRUE(estimate.has_value());
    expect_estimate(*estimate, reports, row);
  }
  const Golden_table queries = table("queries.csv");
  ASSERT_FALSE(queries.rows.empty());
  for (const Row& row : queries.rows)
  {
    const ics::Result<Estimate> estimate = filter.estimate_at(time_of(field_of(queries, row, "time_ns")));
    ASSERT_EQ(estimate.has_value(), integer_of(field_of(queries, row, "ok")) == 1);
    if (estimate.has_value())
    {
      expect_estimate(*estimate, queries, row);
    }
    else
    {
      EXPECT_EQ(estimate.error(), ics::Error::kUnconstrained);
    }
  }
}

TEST(CueGolden, CuesEveryStationAsTheReferenceDoes)
{
  const ics::frames::EnuFrame range = range_of(table("parameters.csv"));
  const Golden_table cues = table("cues.csv");
  ASSERT_FALSE(cues.rows.empty());
  for (const Row& row : cues.rows)
  {
    const ics::Result<ics::frames::Geodetic> site =
      ics::frames::Geodetic::make(ics::Degrees(number_of(field_of(cues, row, "latitude_deg"))),
                                  ics::Degrees(number_of(field_of(cues, row, "longitude_deg"))),
                                  ics::Meters(number_of(field_of(cues, row, "height_m"))));
    ASSERT_TRUE(site.has_value());
    expect_cue(ics::cue::cue_for(cue_estimate(cues, row), ics::cue::station_at(range, *site)), cues, row);
  }
}

TEST(CueGolden, PutsReportsInTheRangeFrameAsTheReferenceDoes)
{
  const ics::frames::EnuFrame range = range_of(table("parameters.csv"));
  const Golden_table measurements = table("measurements.csv");
  ASSERT_FALSE(measurements.rows.empty());
  const ics::cue::Fix_sigmas sigmas{1.0, 2.0};
  const ics::cue::Noise_defaults defaults{sigmas, sigmas, sigmas, sigmas, sigmas, sigmas, 0.5};
  for (const Row& row : measurements.rows)
  {
    const ics::Result<ics::cue::Measurement> report =
      ics::cue::measurement_from(record_of(measurements, row), range, defaults);
    ASSERT_TRUE(report.has_value());
    const ics::cue::Vector3 position = triple(measurements, row, "east_m", "north_m", "up_m");
    const ics::cue::Vector3 velocity =
      triple(measurements, row, "range_velocity_east_m_s", "range_velocity_north_m_s", "range_velocity_up_m_s");
    for (int32 axis = 0; axis < ics::cue::axes; ++axis)
    {
      const std::size_t place = static_cast<std::size_t>(axis);
      EXPECT_NEAR(report->position[place], position[place], position_m);
      EXPECT_NEAR(report->velocity[place], velocity[place], velocity_m_s);
    }
  }
}
