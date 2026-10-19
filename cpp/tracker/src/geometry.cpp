// geometry.cpp: pixels and directions on the sky (ICS-038).
// JSF deviation D-6: ics::Result and ics::encoder::Direction come from .hpp headers.
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

#include <ics/common/error.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/tracker/centroid.h>
#include <ics/tracker/geometry.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::encoder::Direction;

constexpr float64 full_turn = 2.0 * std::numbers::pi;
constexpr std::size_t east = 0U;
constexpr std::size_t north = 1U;
constexpr std::size_t up = 2U;

// East, north and up.
constexpr int32 axes = 3;
using Vector3 = std::array<float64, static_cast<std::size_t>(axes)>;

// The unit vector of a direction.
Vector3 unit_of(const Direction& direction)
{
  const float64 cos_e = std::cos(direction.elevation_rad);
  return {cos_e * std::sin(direction.azimuth_rad), cos_e * std::cos(direction.azimuth_rad),
          std::sin(direction.elevation_rad)};
}

// The directions of increasing azimuth and elevation at a line of sight.
Vector3 along_azimuth(const Direction& line_of_sight)
{
  return {std::cos(line_of_sight.azimuth_rad), -std::sin(line_of_sight.azimuth_rad), 0.0};
}

Vector3 along_elevation(const Direction& line_of_sight)
{
  const float64 sin_e = std::sin(line_of_sight.elevation_rad);
  return {-sin_e * std::sin(line_of_sight.azimuth_rad), -sin_e * std::cos(line_of_sight.azimuth_rad),
          std::cos(line_of_sight.elevation_rad)};
}

// The dot product of two vectors, summed as numpy sums three terms.
float64 dot(const Vector3& left, const Vector3& right)
{
  return ((left[east] * right[east]) + (left[north] * right[north])) + (left[up] * right[up]);
}

}  // namespace

namespace ics::tracker
{

float64 wrap_azimuth(const float64 angle)
{
  float64 turned = std::fmod(angle, full_turn);
  turned = (turned < 0.0) ? (turned + full_turn) : turned;
  return (turned >= full_turn) ? 0.0 : turned;
}

Tangent tangent_of_pixel(const Pixel_point pixel, const Camera_geometry& camera)
{
  const float64 across = pixel.x - camera.boresight_x;
  const float64 right = camera.mirrored ? -across : across;
  const float64 upward = camera.boresight_y - pixel.y;
  const float64 cos_roll = std::cos(camera.roll_rad);
  const float64 sin_roll = std::sin(camera.roll_rad);
  return {camera.plate_scale_rad * ((cos_roll * right) - (sin_roll * upward)),
          camera.plate_scale_rad * ((sin_roll * right) + (cos_roll * upward))};
}

Pixel_point pixel_of_tangent(const Tangent tangent, const Camera_geometry& camera)
{
  const float64 cos_roll = std::cos(camera.roll_rad);
  const float64 sin_roll = std::sin(camera.roll_rad);
  const float64 right = ((cos_roll * tangent.xi) + (sin_roll * tangent.eta)) / camera.plate_scale_rad;
  const float64 upward = ((-sin_roll * tangent.xi) + (cos_roll * tangent.eta)) / camera.plate_scale_rad;
  const float64 across = camera.mirrored ? -right : right;
  return {camera.boresight_x + across, camera.boresight_y - upward};
}

encoder::Direction sky_of_tangent(const Tangent tangent, const encoder::Direction& line_of_sight)
{
  const Vector3 sight = unit_of(line_of_sight);
  const Vector3 azimuth = along_azimuth(line_of_sight);
  const Vector3 elevation = along_elevation(line_of_sight);
  Vector3 point{};
  for (int32 axis = 0; axis < axes; ++axis)
  {
    const std::size_t at = static_cast<std::size_t>(axis);
    point[at] = (sight[at] + (tangent.xi * azimuth[at])) + (tangent.eta * elevation[at]);
  }
  return {wrap_azimuth(std::atan2(point[east], point[north])),
          std::atan2(point[up], std::hypot(point[east], point[north]))};
}

Result<Tangent> tangent_of_sky(const encoder::Direction& direction, const encoder::Direction& line_of_sight)
{
  const Vector3 target = unit_of(direction);
  const float64 depth = dot(target, unit_of(line_of_sight));
  Result<Tangent> tangent = fail(Error::kOutOfRange);
  if (depth > 0.0)
  {
    tangent = Tangent{dot(target, along_azimuth(line_of_sight)) / depth,
                      dot(target, along_elevation(line_of_sight)) / depth};
  }
  return tangent;
}

encoder::Direction sky_of_pixel(const Pixel_point pixel,
                                const encoder::Direction& line_of_sight,
                                const Camera_geometry& camera)
{
  return sky_of_tangent(tangent_of_pixel(pixel, camera), line_of_sight);
}

Result<Pixel_point> pixel_of_sky(const encoder::Direction& direction,
                                 const encoder::Direction& line_of_sight,
                                 const Camera_geometry& camera)
{
  const Result<Tangent> tangent = tangent_of_sky(direction, line_of_sight);
  Result<Pixel_point> pixel = fail(Error::kOutOfRange);
  if (tangent.has_value())
  {
    pixel = pixel_of_tangent(*tangent, camera);
  }
  return pixel;
}

}  // namespace ics::tracker
