#pragma once

#include <chrono>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mount/limits.hpp"

namespace ics::mount {

// What the sun's position needs besides the site and the time.
struct SunSettings {
  // ΔT, how far Terrestrial Time runs ahead of UT1: about 69 s in 2026. An
  // error of a minute moves the sun by under 0.001°.
  Duration delta_t = std::chrono::seconds(69);
  // The site's mean air pressure, in millibars, and temperature, in °C, for
  // refraction.
  double pressure_mbar = 1010.0;
  double temperature_c = 10.0;
};

// The span of UTC times sun_position takes: 2000 to 2100. A clock outside it
// is wrong.
inline constexpr UtcTime kFirstSunTime{std::chrono::sys_days{std::chrono::year{2000} / 1 / 1}};
inline constexpr UtcTime kLastSunTime{std::chrono::sys_days{std::chrono::year{2100} / 1 / 1}};

// The sun's centre seen from the site at a UTC time (ICS-034), by NREL's Solar
// Position Algorithm (SPA), to within 0.0003°: topocentric azimuth, from north
// through east, and altitude, raised by refraction while the sun is up. UTC
// stands in for UT1, which it keeps within 0.9 s of, and the site's height
// above the ellipsoid for its height above sea level; neither moves the sun
// by more than 0.004°. Error::kOutOfRange for a time before kFirstSunTime or
// from kLastSunTime on.
[[nodiscard]] Result<AltAz> sun_position(UtcTime time, const frames::Geodetic& site, const SunSettings& settings);

}  // namespace ics::mount
