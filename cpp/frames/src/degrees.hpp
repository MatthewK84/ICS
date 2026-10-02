#pragma once

// Trigonometry in degrees, exact at multiples of 90 degrees (ICS-017). Ported
// from GeographicLib 2.3's Math::sincosd, Math::atan2d and Math::AngNormalize.
// Copyright (c) 2008-2023, Charles Karney; MIT License, see
// cpp/frames/GEOGRAPHICLIB-LICENSE.txt.

namespace ics::frames::detail {

struct SinCos {
  double sin;
  double cos;
};

// The sine and cosine of an angle in degrees. The angle is reduced exactly to
// [-45, 45] degrees before it is converted to radians, so sin(90) is exactly
// 1 and cos(90) exactly 0.
[[nodiscard]] SinCos sincos_degrees(double degrees) noexcept;

// atan2(y, x) in degrees, in [-180, 180], computed so that the result is
// exact at multiples of 45 degrees.
[[nodiscard]] double atan2_degrees(double y, double x) noexcept;

// An angle wrapped into [-180, 180) degrees.
[[nodiscard]] double wrap_longitude(double degrees) noexcept;

}  // namespace ics::frames::detail
