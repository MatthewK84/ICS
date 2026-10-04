#include "ics/frames/utm.hpp"

#include <array>
#include <cmath>
#include <cstddef>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/frames/wgs84.hpp"

// Ported from GeographicLib 2.3, TransverseMercator::Reverse and Math::tauf,
// under its MIT License (GEOGRAPHICLIB-LICENSE.txt), for the six terms UTM
// needs.

namespace ics::frames {
namespace {

constexpr double kScale = 0.9996;
constexpr double kFalseEasting = 500'000.0;
constexpr double kFalseNorthingSouth = 10'000'000.0;
constexpr double kMaxEasting = 1'000'000.0;
constexpr double kMaxNorthingNorth = 9'600'000.0;
constexpr double kMinNorthingSouth = 1'000'000.0;
constexpr int kZones = 60;
constexpr double kZoneWidthDeg = 6.0;
constexpr double kFirstCentralMeridianDeg = -177.0;
constexpr int kNewtonSteps = 5;

// n = f / (2 - f), the third flattening, and its powers.
constexpr double kN = wgs84::kFlattening / (2.0 - wgs84::kFlattening);
constexpr double kN2 = kN * kN;
constexpr double kN3 = kN2 * kN;
constexpr double kN4 = kN3 * kN;
constexpr double kN5 = kN4 * kN;
constexpr double kN6 = kN5 * kN;

// A, the rectifying radius (Karney 2011, eq. 14).
constexpr double kRectifyingRadius = wgs84::kSemiMajorAxis.value() / (1.0 + kN) *
                                     (1.0 + kN2 / 4.0 + kN4 / 64.0 + kN6 / 256.0);

// The beta series, from the rectifying to the conformal sphere (Karney 2011,
// eq. 36).
constexpr std::array<double, 6> kBeta{
    kN / 2.0 - 2.0 * kN2 / 3.0 + 37.0 * kN3 / 96.0 - kN4 / 360.0 - 81.0 * kN5 / 512.0 + 96199.0 * kN6 / 604800.0,
    kN2 / 48.0 + kN3 / 15.0 - 437.0 * kN4 / 1440.0 + 46.0 * kN5 / 105.0 - 1118711.0 * kN6 / 3870720.0,
    17.0 * kN3 / 480.0 - 37.0 * kN4 / 840.0 - 209.0 * kN5 / 4480.0 + 5569.0 * kN6 / 90720.0,
    4397.0 * kN4 / 161280.0 - 11.0 * kN5 / 504.0 - 830251.0 * kN6 / 7257600.0,
    4583.0 * kN5 / 161280.0 - 108847.0 * kN6 / 3991680.0,
    20648693.0 * kN6 / 638668800.0,
};

// The tangent of the conformal latitude whose geodetic latitude has tangent
// tau (Karney 2011, eq. 7).
[[nodiscard]] double conformal_tangent(const double tau, const double eccentricity) noexcept {
  const double tau1 = std::hypot(1.0, tau);
  const double sigma = std::sinh(eccentricity * std::atanh(eccentricity * tau / tau1));
  return (std::hypot(1.0, sigma) * tau) - (sigma * tau1);
}

// The tangent of the geodetic latitude whose conformal latitude has tangent
// taup, by Newton's method (Karney 2011, eqs. 19 to 21); it converges in two
// or three steps.
[[nodiscard]] double geodetic_tangent(const double taup) noexcept {
  const double eccentricity = std::sqrt(wgs84::kEccentricitySquared);
  constexpr double kE2m = wgs84::kOneMinusEccentricitySquared;
  double tau = taup / kE2m;
  for (int step = 0; step < kNewtonSteps; ++step) {
    const double tau_conformal = conformal_tangent(tau, eccentricity);
    tau += (taup - tau_conformal) * (1.0 + (kE2m * tau * tau)) /
           (kE2m * std::hypot(1.0, tau) * std::hypot(1.0, tau_conformal));
  }
  return tau;
}

[[nodiscard]] bool in_limits(const UtmPoint& point) noexcept {
  const double easting = point.easting.value();
  const double northing = point.northing.value();
  const double lowest = point.north ? 0.0 : kMinNorthingSouth;
  const double highest = point.north ? kMaxNorthingNorth : kFalseNorthingSouth;
  const bool zone = point.zone >= 1 && point.zone <= kZones;
  return zone && easting >= 0.0 && easting <= kMaxEasting && northing >= lowest && northing <= highest;
}

}  // namespace

Result<Geodetic> from_utm(const UtmPoint& point, const Meters height) noexcept {
  if (!in_limits(point)) {
    return fail(Error::kInvalidArgument);
  }
  const double false_northing = point.north ? 0.0 : kFalseNorthingSouth;
  const double xi = (point.northing.value() - false_northing) / (kScale * kRectifyingRadius);
  const double eta = (point.easting.value() - kFalseEasting) / (kScale * kRectifyingRadius);
  double xi_conformal = xi;
  double eta_conformal = eta;
  for (std::size_t j = 0; j < kBeta.size(); ++j) {
    const double order = 2.0 * static_cast<double>(j + 1);
    xi_conformal -= kBeta.at(j) * std::sin(order * xi) * std::cosh(order * eta);
    eta_conformal -= kBeta.at(j) * std::cos(order * xi) * std::sinh(order * eta);
  }
  const double sinh_eta = std::sinh(eta_conformal);
  const double cos_xi = std::cos(xi_conformal);
  const double taup = std::sin(xi_conformal) / std::hypot(sinh_eta, cos_xi);
  const Degrees latitude = to_degrees(Radians(std::atan(geodetic_tangent(taup))));
  const Degrees central_meridian(kFirstCentralMeridianDeg + (kZoneWidthDeg * static_cast<double>(point.zone - 1)));
  const Degrees longitude = central_meridian + to_degrees(Radians(std::atan2(sinh_eta, cos_xi)));
  return Geodetic::make(latitude, longitude, height);
}

}  // namespace ics::frames
