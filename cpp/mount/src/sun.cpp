#include "ics/mount/sun.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

#include "ics/common/check.hpp"
#include "spa_terms.hpp"

// NREL's Solar Position Algorithm (Reda and Andreas, NREL/TP-560-34302,
// revised 2008), step by step as the report gives it, in degrees as the report
// works. Each function is one step or a few.
namespace ics::mount {
namespace {

constexpr double kJ2000 = 2451545.0;
constexpr double kUnixEpochJulianDay = 2440587.5;
constexpr double kSecondsPerDay = 86400.0;
constexpr double kDaysPerCentury = 36525.0;
constexpr double kTermScale = 1e8;
constexpr double kDegreesPerTurn = 360.0;
constexpr double kArcsecondsPerDegree = 3600.0;
// The nutation terms are in units of 0.0001 arcseconds.
constexpr double kNutationScale = 36000000.0;
// The Earth's equatorial radius, in metres, and its polar flattening, as the
// report gives them.
constexpr double kEarthRadiusM = 6378140.0;
constexpr double kPolarRatio = 0.99664719;
// Refraction applies while the sun's upper limb is above the horizon: its
// angular radius plus the refraction at the horizon, in degrees.
constexpr double kSunRadiusDeg = 0.26667;
constexpr double kHorizonRefractionDeg = 0.5667;

[[nodiscard]] double radians(const double degrees) noexcept { return degrees * std::numbers::pi / 180.0; }
[[nodiscard]] double degrees(const double radians) noexcept { return radians * 180.0 / std::numbers::pi; }
[[nodiscard]] double wrap_360(const double degrees) noexcept {
  return degrees - (kDegreesPerTurn * std::floor(degrees / kDegreesPerTurn));
}
// An arcsine whose argument rounding may have carried just past ±1.
[[nodiscard]] double safe_asin(const double x) noexcept { return std::asin(std::clamp(x, -1.0, 1.0)); }

// The report's times: the Julian day, and the Julian century, ephemeris
// century and ephemeris millennium from J2000.0.
struct Times {
  double jd = 0.0;
  double jc = 0.0;
  double jce = 0.0;
  double jme = 0.0;
};

[[nodiscard]] Times times(const UtcTime time, const Duration delta_t) noexcept {
  const double seconds = std::chrono::duration<double>(time.time_since_epoch()).count();
  const double jd = (seconds / kSecondsPerDay) + kUnixEpochJulianDay;
  const double jde = jd + (std::chrono::duration<double>(delta_t).count() / kSecondsPerDay);
  const double jce = (jde - kJ2000) / kDaysPerCentury;
  // 2000 to 2100 is 0 to 0.1 millennia from J2000.0.
  static_cast<void>(ics::check(std::abs(jce / 10.0) < 0.2));
  return {.jd = jd, .jc = (jd - kJ2000) / kDaysPerCentury, .jce = jce, .jme = jce / 10.0};
}

// Σ A·cos(B + C·τ) over one table.
[[nodiscard]] double sum_terms(const std::span<const spa::PeriodicTerm> terms, const double jme) noexcept {
  static_cast<void>(ics::check(!terms.empty()));
  double sum = 0.0;
  for (const spa::PeriodicTerm& term : terms) {
    sum += term.a * std::cos(term.b + (term.c * jme));
  }
  return sum;
}

// Σ table_i·τ^i over the tables of one quantity, scaled out of 10⁻⁸.
[[nodiscard]] double series(const std::span<const std::span<const spa::PeriodicTerm>> tables, const double jme) noexcept {
  static_cast<void>(ics::check(!tables.empty()));
  double sum = 0.0;
  for (std::size_t i = tables.size(); i > 0; --i) {
    sum = (sum * jme) + sum_terms(tables[i - 1], jme);
  }
  return sum / kTermScale;
}

// The Earth's heliocentric longitude and latitude, in degrees, and its
// distance from the sun, in astronomical units.
struct Heliocentric {
  double longitude = 0.0;
  double latitude = 0.0;
  double radius = 0.0;
};

[[nodiscard]] Heliocentric heliocentric(const double jme) noexcept {
  const std::array<std::span<const spa::PeriodicTerm>, 6> l{spa::kL0, spa::kL1, spa::kL2,
                                                              spa::kL3, spa::kL4, spa::kL5};
  const std::array<std::span<const spa::PeriodicTerm>, 2> b{spa::kB0, spa::kB1};
  const std::array<std::span<const spa::PeriodicTerm>, 5> r{spa::kR0, spa::kR1, spa::kR2, spa::kR3, spa::kR4};
  const double radius = series(r, jme);
  // The Earth is 0.983 to 1.017 astronomical units from the sun.
  static_cast<void>(ics::check(std::abs(radius - 1.0) < 0.02));
  return {.longitude = wrap_360(degrees(series(l, jme))), .latitude = degrees(series(b, jme)), .radius = radius};
}

// The nutation in longitude and in obliquity, in degrees.
struct Nutation {
  double longitude = 0.0;
  double obliquity = 0.0;
};

[[nodiscard]] Nutation nutation(const double jce) noexcept {
  const double jce2 = jce * jce;
  const double jce3 = jce2 * jce;
  // The mean elongation of the moon from the sun, the mean anomalies of the
  // sun and the moon, the moon's argument of latitude, and the longitude of
  // its ascending node.
  const std::array<double, 5> x{
      297.85036 + (445267.111480 * jce) - (0.0019142 * jce2) + (jce3 / 189474.0),
      357.52772 + (35999.050340 * jce) - (0.0001603 * jce2) - (jce3 / 300000.0),
      134.96298 + (477198.867398 * jce) + (0.0086972 * jce2) + (jce3 / 56250.0),
      93.27191 + (483202.017538 * jce) - (0.0036825 * jce2) + (jce3 / 327270.0),
      125.04452 - (1934.136261 * jce) + (0.0020708 * jce2) + (jce3 / 450000.0),
  };
  Nutation out;
  for (const spa::NutationTerm& term : spa::kNutation) {
    double argument = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
      argument += term.y[i] * x[i];
    }
    out.longitude += (term.a + (term.b * jce)) * std::sin(radians(argument));
    out.obliquity += (term.c + (term.d * jce)) * std::cos(radians(argument));
  }
  // Nutation in longitude stays within 18 arcseconds.
  static_cast<void>(ics::check(std::abs(out.longitude / kNutationScale) < 0.01));
  return {.longitude = out.longitude / kNutationScale, .obliquity = out.obliquity / kNutationScale};
}

// The true obliquity of the ecliptic, in degrees.
[[nodiscard]] double obliquity(const double jme, const double nutation_obliquity) noexcept {
  constexpr std::array<double, 11> kMean{84381.448, -4680.93, -1.55,  1999.25, -51.38, -249.67,
                                         -39.05,    7.12,     27.87, 5.79,    2.45};
  const double u = jme / 10.0;
  double mean = 0.0;
  for (std::size_t i = kMean.size(); i > 0; --i) {
    mean = (mean * u) + kMean[i - 1];
  }
  const double epsilon = (mean / kArcsecondsPerDegree) + nutation_obliquity;
  // About 23.44° this century.
  static_cast<void>(ics::check(std::abs(epsilon - 23.44) < 0.1));
  return epsilon;
}

// The sun seen from the Earth's centre: right ascension and declination, the
// apparent sidereal time at Greenwich, all in degrees, and the sun's
// distance, in astronomical units.
struct Geocentric {
  double right_ascension = 0.0;
  double declination = 0.0;
  double sidereal_time = 0.0;
  double radius = 0.0;
};

[[nodiscard]] Geocentric geocentric(const Times& t) noexcept {
  const Heliocentric earth = heliocentric(t.jme);
  const double longitude = wrap_360(earth.longitude + 180.0);
  const double latitude = radians(-earth.latitude);
  const Nutation n = nutation(t.jce);
  const double epsilon = radians(obliquity(t.jme, n.obliquity));
  const double aberration = -20.4898 / (kArcsecondsPerDegree * earth.radius);
  const double lambda = radians(longitude + n.longitude + aberration);
  const double mean_sidereal = wrap_360(280.46061837 + (360.98564736629 * (t.jd - kJ2000)) +
                                        (0.000387933 * t.jc * t.jc) - (t.jc * t.jc * t.jc / 38710000.0));
  const double alpha = std::atan2((std::sin(lambda) * std::cos(epsilon)) - (std::tan(latitude) * std::sin(epsilon)),
                                  std::cos(lambda));
  const double delta = safe_asin((std::sin(latitude) * std::cos(epsilon)) +
                                 (std::cos(latitude) * std::sin(epsilon) * std::sin(lambda)));
  // The sun stays within the obliquity of the equator.
  static_cast<void>(ics::check(std::abs(degrees(delta)) < 23.5));
  return {.right_ascension = wrap_360(degrees(alpha)),
          .declination = degrees(delta),
          .sidereal_time = mean_sidereal + (n.longitude * std::cos(epsilon)),
          .radius = earth.radius};
}

// The sun seen from the site: the topocentric hour angle and declination,
// and the altitude before refraction, all in degrees.
struct Topocentric {
  double hour_angle = 0.0;
  double declination = 0.0;
  double altitude = 0.0;
};

[[nodiscard]] Topocentric topocentric(const Geocentric& sun, const frames::Geodetic& site) noexcept {
  const double phi = radians(site.latitude().value());
  const double hour_angle = radians(wrap_360(sun.sidereal_time + site.longitude().value() - sun.right_ascension));
  const double delta = radians(sun.declination);
  const double parallax = radians(8.794 / (kArcsecondsPerDegree * sun.radius));
  const double u = std::atan(kPolarRatio * std::tan(phi));
  const double height = site.height().value() / kEarthRadiusM;
  const double x = std::cos(u) + (height * std::cos(phi));
  const double y = (kPolarRatio * std::sin(u)) + (height * std::sin(phi));
  const double below = std::cos(delta) - (x * std::sin(parallax) * std::cos(hour_angle));
  const double delta_alpha = std::atan2(-x * std::sin(parallax) * std::sin(hour_angle), below);
  const double declination = std::atan2((std::sin(delta) - (y * std::sin(parallax))) * std::cos(delta_alpha), below);
  // Parallax moves the sun by under 9 arcseconds.
  static_cast<void>(ics::check(std::abs(degrees(declination - delta)) < 0.01));
  const double h = hour_angle - delta_alpha;
  const double altitude =
      safe_asin((std::sin(phi) * std::sin(declination)) + (std::cos(phi) * std::cos(declination) * std::cos(h)));
  return {.hour_angle = degrees(h), .declination = degrees(declination), .altitude = degrees(altitude)};
}

// How far refraction raises the sun at a true altitude, in degrees: nothing
// once its upper limb has set.
[[nodiscard]] double refraction(const double altitude, const SunSettings& settings) noexcept {
  if (altitude < -(kSunRadiusDeg + kHorizonRefractionDeg)) {
    return 0.0;
  }
  const double raised = (settings.pressure_mbar / 1010.0) * (283.0 / (273.0 + settings.temperature_c)) * 1.02 /
                        (60.0 * std::tan(radians(altitude + (10.3 / (altitude + 5.11)))));
  // Refraction raises the sun by under a degree, with sane air.
  static_cast<void>(ics::check(std::abs(raised) < 1.0));
  return raised;
}

// The azimuth from north through east, in degrees.
[[nodiscard]] double azimuth(const Topocentric& sun, const frames::Geodetic& site) noexcept {
  const double h = radians(sun.hour_angle);
  const double phi = radians(site.latitude().value());
  const double gamma = std::atan2(std::sin(h), (std::cos(h) * std::sin(phi)) -
                                                   (std::tan(radians(sun.declination)) * std::cos(phi)));
  return wrap_360(degrees(gamma) + 180.0);
}

}  // namespace

Result<AltAz> sun_position(const UtcTime time, const frames::Geodetic& site, const SunSettings& settings) {
  if (time < kFirstSunTime || time >= kLastSunTime) {
    return fail(Error::kOutOfRange);
  }
  const Topocentric sun = topocentric(geocentric(times(time, settings.delta_t)), site);
  const double altitude = sun.altitude + refraction(sun.altitude, settings);
  return AltAz{.altitude = to_radians(Degrees(altitude)), .azimuth = to_radians(Degrees(azimuth(sun, site)))};
}

}  // namespace ics::mount
