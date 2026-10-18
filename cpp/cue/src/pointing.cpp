// pointing.cpp: azimuth, elevation, their rates and the pointing uncertainty (ICS-037).
// JSF deviation D-6: ics::fail, ics::Meters and ics::frames come from .hpp headers.
#include <cmath>
#include <cstddef>
#include <numbers>

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
using ics::cue::Rotation;
using ics::cue::Vector3;

// Nearer than these, horizontally or in all, the direction is not defined well enough to cue.
constexpr float64 min_horizontal_m = 0.01;
constexpr float64 min_range_m = 1.0;
constexpr float64 full_turn = 2.0 * std::numbers::pi;

// A 3 by 3 position covariance, row by row.
using Spread = Rotation;

// The position of row and column in a 3 by 3 matrix.
std::size_t cell(const int32 row, const int32 column)
{
  return static_cast<std::size_t>((row * ics::cue::axes) + column);
}

// The value of a vector at an index.
float64 value_of(const Vector3& vector, const int32 position)
{
  return vector[static_cast<std::size_t>(position)];
}

// rotation times vector.
Vector3 turn(const Rotation& rotation, const Vector3& vector)
{
  Vector3 turned{};
  for (int32 row = 0; row < ics::cue::axes; ++row)
  {
    float64 sum = 0.0;
    for (int32 column = 0; column < ics::cue::axes; ++column)
    {
      sum += rotation[cell(row, column)] * value_of(vector, column);
    }
    turned[static_cast<std::size_t>(row)] = sum;
  }
  return turned;
}

// The position covariance of an estimate, in the station's axes: R P R^T.
Spread turned_spread(const ics::cue::Matrix& covariance, const Rotation& rotation)
{
  Spread turned{};
  for (int32 row = 0; row < ics::cue::axes; ++row)
  {
    for (int32 column = 0; column < ics::cue::axes; ++column)
    {
      float64 sum = 0.0;
      for (int32 left = 0; left < ics::cue::axes; ++left)
      {
        for (int32 right = 0; right < ics::cue::axes; ++right)
        {
          sum += rotation[cell(row, left)] * covariance.at(left, right) * rotation[cell(column, right)];
        }
      }
      turned[cell(row, column)] = sum;
    }
  }
  return turned;
}

// u^T spread v.
float64 between(const Vector3& left,
                const Spread& spread,
                const Vector3& right)
{
  float64 sum = 0.0;
  for (int32 row = 0; row < ics::cue::axes; ++row)
  {
    for (int32 column = 0; column < ics::cue::axes; ++column)
    {
      sum += value_of(left, row) * spread[cell(row, column)] * value_of(right, column);
    }
  }
  return sum;
}

// The one-sigma position error across the line of sight, along its larger axis, in metres.
float64 transverse_sigma(const Spread& spread,
                         const float64 azimuth,
                         const float64 elevation)
{
  const Vector3 along_azimuth{std::cos(azimuth), -std::sin(azimuth), 0.0};
  const Vector3 along_elevation{-std::sin(azimuth) * std::sin(elevation),
                                -std::cos(azimuth) * std::sin(elevation),
                                std::cos(elevation)};
  const float64 first = between(along_azimuth, spread, along_azimuth);
  const float64 second = between(along_elevation, spread, along_elevation);
  const float64 cross = between(along_azimuth, spread, along_elevation);
  const float64 half_difference = (first - second) / 2.0;
  const float64 largest = ((first + second) / 2.0) + std::sqrt((half_difference * half_difference) + (cross * cross));
  return std::sqrt((largest > 0.0) ? largest : 0.0);
}

// The column of the station's rotation for one range axis.
Vector3 column_for(const ics::frames::EnuFrame& range,
                   const ics::frames::EnuFrame& here,
                   const int32 axis)
{
  const ics::frames::Enu unit{ics::Meters((axis == 0) ? 1.0 : 0.0),
                              ics::Meters((axis == 1) ? 1.0 : 0.0),
                              ics::Meters((axis == 2) ? 1.0 : 0.0)};
  const ics::frames::Enu turned = here.rotate(range.rotate(unit));
  return {turned.east.value(), turned.north.value(), turned.up.value()};
}

}  // namespace

namespace ics::cue
{

Station station_at(const frames::EnuFrame& range, const frames::Geodetic& site)
{
  const frames::EnuFrame here(site);
  const frames::Enu position = range.to_enu(site);
  Station station{{position.east.value(), position.north.value(), position.up.value()}, {}};
  for (int32 column = 0; column < axes; ++column)
  {
    const Vector3 turned = column_for(range, here, column);
    for (int32 row = 0; row < axes; ++row)
    {
      station.axes[cell(row, column)] = value_of(turned, row);
    }
  }
  return station;
}

float64 wrap_azimuth(const float64 angle)
{
  const float64 turned = (angle < 0.0) ? (angle + full_turn) : angle;
  return (turned >= full_turn) ? 0.0 : turned;
}

Result<Cue> cue_for(const Estimate& estimate, const Station& station)
{
  Vector3 offset{};
  Vector3 moving{};
  for (int32 axis = 0; axis < axes; ++axis)
  {
    offset[static_cast<std::size_t>(axis)] = component(estimate.state, axis) - value_of(station.position, axis);
    moving[static_cast<std::size_t>(axis)] = component(estimate.state, index(1, axis));
  }
  const Vector3 seen = turn(station.axes, offset);
  const Vector3 velocity = turn(station.axes, moving);
  const float64 east = value_of(seen, 0);
  const float64 north = value_of(seen, 1);
  const float64 up = value_of(seen, 2);
  const float64 horizontal = std::hypot(east, north);
  const float64 distance = std::hypot(horizontal, up);
  Result<Cue> cue = fail(Error::kUnconstrained);
  if ((horizontal >= min_horizontal_m) && (distance >= min_range_m))
  {
    const float64 azimuth = wrap_azimuth(std::atan2(east, north));
    const float64 elevation = std::atan2(up, horizontal);
    const float64 across = (north * value_of(velocity, 0)) - (east * value_of(velocity, 1));
    const float64 outward = (east * value_of(velocity, 0)) + (north * value_of(velocity, 1));
    const float64 climbing = ((horizontal * horizontal) * value_of(velocity, 2)) - (up * outward);
    const Spread spread = turned_spread(estimate.covariance, station.axes);
    cue = Cue{estimate.time,
              azimuth,
              elevation,
              across / (horizontal * horizontal),
              climbing / (horizontal * (distance * distance)),
              distance,
              transverse_sigma(spread, azimuth, elevation) / distance};
  }
  return cue;
}

}  // namespace ics::cue
