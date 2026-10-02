// Follows GeographicLib 2.3's LocalCartesian and Geocentric::Rotation
// (src/LocalCartesian.cpp, src/Geocentric.cpp). Copyright (c) 2008-2023,
// Charles Karney; MIT License, see cpp/frames/GEOGRAPHICLIB-LICENSE.txt.
#include "ics/frames/enu.hpp"

#include <array>

#include "degrees.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"

namespace ics::frames {
namespace {

std::array<double, 9> rotation_at(const Geodetic& origin) noexcept {
  const detail::SinCos phi = detail::sincos_degrees(origin.latitude().value());
  const detail::SinCos lambda = detail::sincos_degrees(origin.longitude().value());
  return {
      -lambda.sin, -lambda.cos * phi.sin, lambda.cos * phi.cos,  //
      lambda.cos,  -lambda.sin * phi.sin, lambda.sin * phi.cos,  //
      0.0,         phi.cos,               phi.sin,
  };
}

}  // namespace

EnuFrame::EnuFrame(const Geodetic& origin) noexcept
    : origin_(origin), origin_ecef_(frames::to_ecef(origin)), rotation_(rotation_at(origin)) {}

std::array<double, 3> EnuFrame::to_enu_axes(const std::array<double, 3>& ecef) const noexcept {
  const std::array<double, 9>& m = rotation_;
  return {
      m[0] * ecef[0] + m[3] * ecef[1] + m[6] * ecef[2],
      m[1] * ecef[0] + m[4] * ecef[1] + m[7] * ecef[2],
      m[2] * ecef[0] + m[5] * ecef[1] + m[8] * ecef[2],
  };
}

std::array<double, 3> EnuFrame::to_ecef_axes(const std::array<double, 3>& enu) const noexcept {
  const std::array<double, 9>& m = rotation_;
  return {
      m[0] * enu[0] + m[1] * enu[1] + m[2] * enu[2],
      m[3] * enu[0] + m[4] * enu[1] + m[5] * enu[2],
      m[6] * enu[0] + m[7] * enu[1] + m[8] * enu[2],
  };
}

Enu EnuFrame::to_enu(const Ecef& position) const noexcept {
  return rotate(Ecef{position.x - origin_ecef_.x, position.y - origin_ecef_.y, position.z - origin_ecef_.z});
}

Ecef EnuFrame::to_ecef(const Enu& position) const noexcept {
  const Ecef offset = rotate(position);
  return {origin_ecef_.x + offset.x, origin_ecef_.y + offset.y, origin_ecef_.z + offset.z};
}

}  // namespace ics::frames
