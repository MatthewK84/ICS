// pointing_test.cpp: azimuth, elevation, rates and sigma, and station axes (ICS-037).
#include <chrono>
#include <cmath>
#include <cstddef>
#include <numbers>

#include <gtest/gtest.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/pointing.h>
#include <ics/frames/enu.hpp>
#include <ics/frames/geodetic.hpp>

namespace
{

using ics::float64;
using ics::int32;
using ics::cue::Estimate;

constexpr float64 same_rad = 1.0E-12;
constexpr float64 rate_rad_s = 1.0E-7;
constexpr float64 step_s = 0.001;
constexpr ics::cue::Process_noise still{0.0, 0.0};

ics::frames::Geodetic point(const float64 latitude, const float64 longitude, const float64 height)
{
  const ics::Result<ics::frames::Geodetic> made =
    ics::frames::Geodetic::make(ics::Degrees(latitude), ics::Degrees(longitude), ics::Meters(height));
  EXPECT_TRUE(made.has_value());
  return *made;
}

ics::frames::EnuFrame range()
{
  return ics::frames::EnuFrame(point(40.0, -100.0, 700.0));
}

Estimate estimate_at(const ics::cue::Vector3& position,
                     const ics::cue::Vector3& velocity,
                     const float64 sigma)
{
  Estimate estimate{ics::UtcTime{}, {}, ics::cue::Matrix::identity()};
  for (int32 axis = 0; axis < ics::cue::axes; ++axis)
  {
    ics::cue::set_component(estimate.state, axis, position[static_cast<std::size_t>(axis)]);
    ics::cue::set_component(estimate.state, ics::cue::index(1, axis), velocity[static_cast<std::size_t>(axis)]);
    estimate.covariance.put(axis, axis, sigma * sigma);
  }
  return estimate;
}

ics::cue::Cue cue_of(const Estimate& estimate, const ics::cue::Station& station)
{
  const ics::Result<ics::cue::Cue> cue = ics::cue::cue_for(estimate, station);
  EXPECT_TRUE(cue.has_value());
  return cue.value_or(ics::cue::Cue{});
}

ics::UtcTime after(const float64 seconds)
{
  return ics::UtcTime{} + std::chrono::duration_cast<ics::Duration>(std::chrono::duration<float64>(seconds));
}

}  // namespace

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(CuePointing, AStationAtTheOriginHasTheRangeAxes)
{
  const ics::cue::Station station = ics::cue::station_at(range(), point(40.0, -100.0, 700.0));
  EXPECT_NEAR(station.position[0], 0.0, 1.0E-9);
  EXPECT_NEAR(station.axes[0], 1.0, same_rad);
  EXPECT_NEAR(station.axes[4], 1.0, same_rad);
  EXPECT_NEAR(station.axes[1], 0.0, same_rad);
}

TEST(CuePointing, AStationsAxesTurnWithTheNormal)
{
  const ics::cue::Station east = ics::cue::station_at(range(), point(40.0, -99.9965, 702.0));
  EXPECT_NEAR(std::acos(east.axes[8]), 298.0 / 6.38E6, 1.0E-6);
}

TEST(CuePointing, AzimuthRunsClockwiseFromNorth)
{
  const ics::cue::Station station = ics::cue::station_at(range(), point(40.0, -100.0, 700.0));
  const ics::cue::Cue north = cue_of(estimate_at({0.0, 100.0, 0.0}, {}, 1.0), station);
  EXPECT_NEAR(std::remainder(north.azimuth_rad, 2.0 * std::numbers::pi), 0.0, same_rad);
  const ics::cue::Cue east = cue_of(estimate_at({100.0, 0.0, 100.0}, {}, 1.0), station);
  EXPECT_NEAR(east.azimuth_rad, std::numbers::pi / 2.0, same_rad);
  EXPECT_NEAR(east.elevation_rad, std::numbers::pi / 4.0, same_rad);
  const ics::cue::Cue west = cue_of(estimate_at({-100.0, 0.0, 0.0}, {}, 1.0), station);
  EXPECT_NEAR(west.azimuth_rad, 3.0 * std::numbers::pi / 2.0, same_rad);
}

TEST(CuePointing, WrapsATinyNegativeAngleToNorth)
{
  EXPECT_DOUBLE_EQ(ics::cue::wrap_azimuth(-1.0E-17), 0.0);
  EXPECT_DOUBLE_EQ(ics::cue::wrap_azimuth(1.0), 1.0);
  EXPECT_NEAR(ics::cue::wrap_azimuth(-std::numbers::pi / 2.0), 3.0 * std::numbers::pi / 2.0, same_rad);
}

TEST(CuePointing, RatesAreTheAnglesDerivatives)
{
  const ics::cue::Station station = ics::cue::station_at(range(), point(39.998, -100.0015, 702.0));
  const Estimate now = estimate_at({30.0, -40.0, 35.0}, {5.0, -2.0, 1.5}, 1.0);
  const ics::cue::Cue cue = cue_of(ics::cue::predict(now, after(step_s), still), station);
  const ics::cue::Cue early = cue_of(now, station);
  const ics::cue::Cue late = cue_of(ics::cue::predict(now, after(2.0 * step_s), still), station);
  EXPECT_NEAR(cue.azimuth_rate_rad_s, (late.azimuth_rad - early.azimuth_rad) / (2.0 * step_s), rate_rad_s);
  EXPECT_NEAR(cue.elevation_rate_rad_s, (late.elevation_rad - early.elevation_rad) / (2.0 * step_s), rate_rad_s);
}

TEST(CuePointing, SigmaIsTheTransverseErrorOverRange)
{
  const ics::cue::Station station = ics::cue::station_at(range(), point(40.0, -100.0, 700.0));
  const ics::cue::Cue round = cue_of(estimate_at({200.0, 150.0, 30.0}, {}, 2.0), station);
  EXPECT_NEAR(round.sigma_rad, 2.0 / round.range_m, 1.0E-12);
  Estimate stretched = estimate_at({0.0, 250.0, 0.0}, {}, 1.0);
  stretched.covariance.put(0, 0, 9.0);
  stretched.covariance.put(1, 1, 100.0);
  EXPECT_NEAR(cue_of(stretched, station).sigma_rad, 3.0 / 250.0, 1.0E-9);
}

TEST(CuePointing, AnExactPositionHasNoSigma)
{
  const ics::cue::Station station = ics::cue::station_at(range(), point(40.0, -100.0, 700.0));
  EXPECT_DOUBLE_EQ(cue_of(estimate_at({200.0, 150.0, 30.0}, {}, 0.0), station).sigma_rad, 0.0);
}

TEST(CuePointing, RefusesATargetOverheadOrAtTheStation)
{
  const ics::cue::Station station = ics::cue::station_at(range(), point(40.0, -100.0, 700.0));
  EXPECT_EQ(ics::cue::cue_for(estimate_at({0.0, 0.0, 50.0}, {}, 1.0), station).error(), ics::Error::kUnconstrained);
  EXPECT_EQ(ics::cue::cue_for(estimate_at({0.3, 0.0, 0.5}, {}, 1.0), station).error(), ics::Error::kUnconstrained);
}
