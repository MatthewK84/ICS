#include "ics/mount/sun.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <numbers>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"

namespace {

using ics::Degrees;
using ics::Error;
using ics::Meters;
using ics::UtcTime;
using ics::frames::Geodetic;
using ics::mount::AltAz;
using ics::mount::kFirstSunTime;
using ics::mount::kLastSunTime;
using ics::mount::sun_position;
using ics::mount::SunSettings;

constexpr double kDeg = std::numbers::pi / 180.0;

Geodetic site(const double lat_deg, const double lon_deg, const double height_m) {
  return Geodetic::make(Degrees(lat_deg), Degrees(lon_deg), Meters(height_m)).value();
}

// One case: where and when, the settings, and the sun's altitude and azimuth
// in degrees.
struct Case {
  std::int64_t utc_ns = 0;
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  double height_m = 0.0;
  double pressure_mbar = 1010.0;
  double temperature_c = 10.0;
  double delta_t_s = 69.0;
  double altitude_deg = 0.0;
  double azimuth_deg = 0.0;
};

AltAz sun_for(const Case& c) {
  const SunSettings settings{.delta_t = std::chrono::duration_cast<ics::Duration>(std::chrono::duration<double>(c.delta_t_s)),
                             .pressure_mbar = c.pressure_mbar,
                             .temperature_c = c.temperature_c};
  return sun_position(ics::utc_from_ns(c.utc_ns), site(c.lat_deg, c.lon_deg, c.height_m), settings).value();
}

// The report's worked example: Golden, Colorado, at 12:30:30 local time
// (UTC−7) on 17 October 2003, where the report gives a zenith angle of
// 50.11162° and an azimuth of 194.34024°.
TEST(SunPosition, MatchesTheReportsWorkedExample) {
  const AltAz sun = sun_for({.utc_ns = 1066419030000000000,
                             .lat_deg = 39.742476,
                             .lon_deg = -105.1786,
                             .height_m = 1830.14,
                             .pressure_mbar = 820.0,
                             .temperature_c = 11.0,
                             .delta_t_s = 67.0});
  EXPECT_NEAR(90.0 - (sun.altitude.value() / kDeg), 50.11162, 1e-4);
  EXPECT_NEAR(sun.azimuth.value() / kDeg, 194.34024, 1e-4);
}

// pvlib 0.16.1's SPA for the same cases: both hemispheres, the sun near the
// zenith, rising through refraction, far below the horizon, and at the ends
// of the span.
TEST(SunPosition, MatchesPvlib) {
  const std::array<Case, 8> cases{{
      {1066419030000000000, 39.742476, -105.1786, 1830.14, 820.0, 11.0, 67.0, 39.888377976, 194.340240510},
      {1782068400000000000, 32.38, -106.48, 1200.0, 1010.0, 10.0, 69.0, 80.895649530, 168.622057578},
      {1955584800000000000, -35.3, 149.1, 580.0, 1010.0, 10.0, 69.0, 78.128687511, 1.499248650},
      {2215836300000000000, 45.0, 0.0, 0.0, 1010.0, 10.0, 69.0, 0.188348991, 89.517134805},
      {2524608000000000000, 51.5, 0.0, 30.0, 1010.0, 10.0, 69.0, -61.490046225, 358.381351875},
      {946728000000000000, 0.0, 0.0, 0.0, 1010.0, 10.0, 69.0, 66.959889931, 178.068892964},
      {4102444799000000000, 60.0, 25.0, 0.0, 1010.0, 10.0, 69.0, -49.310683064, 35.365191032},
      {3328383600000000000, 69.6, 18.9, 10.0, 1010.0, 10.0, 69.0, 3.281592520, 3.109603272},
  }};
  for (const Case& c : cases) {
    const AltAz sun = sun_for(c);
    EXPECT_NEAR(sun.altitude.value() / kDeg, c.altitude_deg, 1e-6) << c.utc_ns;
    EXPECT_NEAR(sun.azimuth.value() / kDeg, c.azimuth_deg, 1e-6) << c.utc_ns;
  }
}

TEST(SunPosition, RefusesTimesOutsideItsSpan) {
  const Geodetic here = site(32.38, -106.48, 1200.0);
  const std::array<UtcTime, 2> outside{kFirstSunTime - std::chrono::nanoseconds(1), kLastSunTime};
  for (const UtcTime time : outside) {
    const auto sun = sun_position(time, here, SunSettings{});
    ASSERT_FALSE(sun.has_value());
    EXPECT_EQ(sun.error(), Error::kOutOfRange);
  }
  EXPECT_TRUE(sun_position(kFirstSunTime, here, SunSettings{}).has_value());
}

}  // namespace
