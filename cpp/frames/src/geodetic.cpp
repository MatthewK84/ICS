// to_ecef and to_geodetic are ported from GeographicLib 2.3's
// Geocentric::IntForward and Geocentric::IntReverse (src/Geocentric.cpp),
// keeping only the oblate-ellipsoid case WGS84 needs. Copyright (c)
// 2008-2023, Charles Karney; MIT License, see
// cpp/frames/GEOGRAPHICLIB-LICENSE.txt.
#include "ics/frames/geodetic.hpp"

#include <cmath>
#include <limits>

#include "degrees.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/wgs84.hpp"

namespace ics::frames {
namespace {

constexpr double kMaxLatitude = 90.0;
constexpr double kA = wgs84::kSemiMajorAxis.value();
constexpr double kE2 = wgs84::kEccentricitySquared;
constexpr double kE2m = wgs84::kOneMinusEccentricitySquared;
constexpr double kE4 = kE2 * kE2;
// Beyond this distance from the centre the Earth is treated as a point,
// which avoids overflow (about 12 million light years).
constexpr double kMaxRadius = 2.0 * kA / std::numeric_limits<double>::epsilon();

struct LatitudeHeight {
  detail::SinCos latitude;
  double height;
};

double square(const double value) noexcept { return value * value; }

// A point too far away for the ellipsoid to matter.
LatitudeHeight far_away(const double r, const double z, const double distance) noexcept {
  // Halve before hypot, so that a finite r and z cannot overflow it.
  const double half_r = r / 2.0;
  const double hypotenuse = std::hypot(z / 2.0, half_r);
  return {{(z / 2.0) / hypotenuse, half_r / hypotenuse}, distance};
}

// A point on the equatorial plane inside the ellipsoid's evolute, where the
// general solution divides 0 by 0: take its limit.
LatitudeHeight equatorial_inside(const double p, const double z) noexcept {
  const double zz = std::sqrt((kE4 - p) / kE2m);
  const double xx = std::sqrt(p);
  const double hypotenuse = std::hypot(zz, xx);
  const double sign = z < 0.0 ? -1.0 : 1.0;
  return {{sign * zz / hypotenuse, xx / hypotenuse}, -kA * kE2m * hypotenuse / kE2};
}

// The real root u of the quartic in Vermeille's method, chosen to avoid
// cancellation.
double quartic_root(const double p, const double q, const double r) noexcept {
  const double s = kE4 * p * q / 4.0;  // s = r^3 * Vermeille's s
  const double r2 = square(r);
  const double r3 = r * r2;
  const double disc = s * (2.0 * r3 + s);
  if (disc < 0.0) {
    // t is complex, but u is real.
    const double angle = std::atan2(std::sqrt(-disc), -(s + r3));
    return r + 2.0 * r * std::cos(angle / 3.0);
  }
  // The sign of the square root maximizes |t3|, to avoid cancellation.
  const double t3 = s + r3 + std::copysign(std::sqrt(disc), s + r3);
  const double t = std::cbrt(t3);
  // GeographicLib guards t == 0, which needs s == 0 and r == 0: a point on
  // the axis with e2m (z/a)^2 equal to e^4, or one on the equator with
  // (r/a)^2 equal to e^4, to the last bit. No double does either for WGS84,
  // and the equatorial plane with r <= 0 takes equatorial_inside instead.
  return r + t + r2 / t;
}

LatitudeHeight general(const double r, const double z, const double p, const double q, const double rr) noexcept {
  const double u = quartic_root(p, q, rr);
  const double v = std::sqrt(square(u) + kE4 * q);
  // u + v, rearranged when u < 0 to avoid losing accuracy.
  const double uv = u < 0.0 ? kE4 * q / (v - u) : u + v;
  const double w = std::fmax(0.0, kE2 * (uv - q) / (2.0 * v));
  const double k = uv / (std::sqrt(uv + square(w)) + w);
  const double k2 = k + kE2;
  const double d = k * r / k2;
  const double hypotenuse = std::hypot(z / k, r / k2);
  return {{(z / k) / hypotenuse, (r / k2) / hypotenuse}, (1.0 - kE2m / k) * std::hypot(d, z)};
}

LatitudeHeight near(const double r, const double z) noexcept {
  const double p = square(r / kA);
  const double q = kE2m * square(z / kA);
  const double rr = (p + q - kE4) / 6.0;
  if (kE4 * q == 0.0 && rr <= 0.0) {
    return equatorial_inside(p, z);
  }
  return general(r, z, p, q, rr);
}

}  // namespace

Geodetic::Geodetic(const Degrees latitude, const Degrees longitude, const Meters height) noexcept
    : latitude_(latitude), longitude_(longitude), height_(height) {}

Result<Geodetic> Geodetic::make(const Degrees latitude, const Degrees longitude, const Meters height) noexcept {
  if (!std::isfinite(latitude.value()) || !std::isfinite(longitude.value()) || !std::isfinite(height.value())) {
    return fail(Error::kInvalidArgument);
  }
  if (std::fabs(latitude.value()) > kMaxLatitude) {
    return fail(Error::kOutOfRange);
  }
  return Geodetic(latitude, Degrees(detail::wrap_longitude(longitude.value())), height);
}

Ecef to_ecef(const Geodetic& point) noexcept {
  const detail::SinCos phi = detail::sincos_degrees(point.latitude().value());
  const detail::SinCos lambda = detail::sincos_degrees(point.longitude().value());
  const double h = point.height().value();
  // The prime vertical radius of curvature.
  const double n = kA / std::sqrt(1.0 - kE2 * square(phi.sin));
  const double across = (n + h) * phi.cos;
  return {Meters(across * lambda.cos), Meters(across * lambda.sin), Meters((kE2m * n + h) * phi.sin)};
}

Result<Geodetic> to_geodetic(const Ecef& position) noexcept {
  const double x = position.x.value();
  const double y = position.y.value();
  const double z = position.z.value();
  const double r = std::hypot(x, y);
  const double distance = std::hypot(r, z);
  // A NaN distance takes near(), whose NaN results make() rejects.
  const LatitudeHeight result = distance > kMaxRadius ? far_away(r, z, distance) : near(r, z);
  const double latitude = detail::atan2_degrees(result.latitude.sin, result.latitude.cos);
  return Geodetic::make(Degrees(latitude), Degrees(detail::atan2_degrees(y, x)), Meters(result.height));
}

}  // namespace ics::frames
