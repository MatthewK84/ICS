// Ported from GeographicLib 2.3's Math::sincosd, Math::atan2d and
// Math::AngNormalize (src/Math.cpp). Copyright (c) 2008-2023, Charles Karney;
// MIT License, see cpp/frames/GEOGRAPHICLIB-LICENSE.txt.
#include "degrees.hpp"

#include <cmath>
#include <numbers>

namespace ics::frames::detail {
namespace {

constexpr double kQuarterTurn = 90.0;
constexpr double kHalfTurn = 180.0;
constexpr double kFullTurn = 360.0;
constexpr double kRadiansPerDegree = std::numbers::pi / kHalfTurn;
constexpr unsigned kQuadrantMask = 3U;

}  // namespace

SinCos sincos_degrees(const double degrees) noexcept {
  int quadrant = 0;
  // remquo is exact: the remainder lies in [-45, 45].
  const double reduced = std::remquo(degrees, kQuarterTurn, &quadrant) * kRadiansPerDegree;
  const double s = std::sin(reduced);
  const double c = std::cos(reduced);
  SinCos result{s, c};
  switch (static_cast<unsigned>(quadrant) & kQuadrantMask) {
    case 0U:
      break;
    case 1U:
      result = {c, -s};
      break;
    case 2U:
      result = {-s, -c};
      break;
    default:
      result = {-c, s};
      break;
  }
  // As in C's Annex F: the cosine is never -0, and a zero sine takes the
  // angle's sign.
  result.cos += 0.0;
  if (result.sin == 0.0) {
    result.sin = std::copysign(result.sin, degrees);
  }
  return result;
}

double atan2_degrees(const double y, const double x) noexcept {
  // Map the arguments so that atan2 returns an angle in [-45, 45] degrees,
  // then map that angle back to its quadrant.
  const bool swapped = std::fabs(y) > std::fabs(x);
  const double along = swapped ? y : x;
  const double across = swapped ? x : y;
  const bool negative = std::signbit(along);
  const double angle = std::atan2(across, std::fabs(along)) / kRadiansPerDegree;
  if (!swapped) {
    return negative ? std::copysign(kHalfTurn, across) - angle : angle;
  }
  return negative ? -kQuarterTurn + angle : kQuarterTurn - angle;
}

double wrap_longitude(const double degrees) noexcept {
  const double wrapped = std::remainder(degrees, kFullTurn);
  return wrapped == kHalfTurn ? -kHalfTurn : wrapped;
}

}  // namespace ics::frames::detail
