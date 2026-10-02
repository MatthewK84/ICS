#pragma once

#include "ics/common/units.hpp"

// The WGS84 ellipsoid, as docs/frames-and-time.md defines it (ICS-017).
namespace ics::frames::wgs84 {

// a, the equatorial radius.
inline constexpr Meters kSemiMajorAxis{6'378'137.0};
// f = (a - b) / a.
inline constexpr double kFlattening = 1.0 / 298.257223563;
// e² = f(2 - f), the first eccentricity squared.
inline constexpr double kEccentricitySquared = kFlattening * (2.0 - kFlattening);
// 1 - e² = (1 - f)².
inline constexpr double kOneMinusEccentricitySquared = (1.0 - kFlattening) * (1.0 - kFlattening);
// b = a(1 - f), the polar radius.
inline constexpr Meters kSemiMinorAxis = kSemiMajorAxis * (1.0 - kFlattening);

}  // namespace ics::frames::wgs84
