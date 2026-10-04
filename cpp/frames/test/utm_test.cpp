#include "ics/frames/utm.hpp"

#include <limits>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/testing/no_allocation_scope.hpp"

namespace {

using ics::Meters;
using ics::frames::Geodetic;
using ics::frames::UtmPoint;

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();

UtmPoint utm(const int zone, const bool north, const double easting, const double northing) {
  return UtmPoint{.zone = zone, .north = north, .easting = Meters(easting), .northing = Meters(northing)};
}

Geodetic convert(const UtmPoint& point) {
  const ics::Result<Geodetic> result = ics::frames::from_utm(point, Meters(0.0));
  EXPECT_TRUE(result.has_value()) << point.zone << " " << point.easting.value() << " " << point.northing.value();
  return result.value_or(Geodetic::make(ics::Degrees(0), ics::Degrees(0), Meters(0)).value());
}

TEST(Utm, RejectsPositionsOutsideUtm) {
  for (const UtmPoint& point :
       {utm(0, true, 500'000.0, 0.0), utm(61, true, 500'000.0, 0.0), utm(31, true, -0.1, 0.0),
        utm(31, true, 1'000'000.1, 0.0), utm(31, true, 500'000.0, -0.1), utm(31, true, 500'000.0, 9'600'000.1),
        utm(31, false, 500'000.0, 999'999.9), utm(31, false, 500'000.0, 10'000'000.1), utm(31, true, kNan, 0.0),
        utm(31, false, 500'000.0, kNan), utm(31, true, kInfinity, 0.0)}) {
    const ics::Result<Geodetic> result = ics::frames::from_utm(point, Meters(0.0));
    ASSERT_FALSE(result.has_value()) << point.zone << " " << point.easting.value() << " " << point.northing.value();
    EXPECT_EQ(result.error(), ics::Error::kInvalidArgument);
  }
}

TEST(Utm, RejectsAHeightThatIsNotFinite) {
  EXPECT_EQ(ics::frames::from_utm(utm(31, true, 500'000.0, 0.0), Meters(kNan)).error(),
            ics::Error::kInvalidArgument);
}

TEST(Utm, AcceptsItsLimits) {
  for (const UtmPoint& point : {utm(1, true, 0.0, 0.0), utm(60, true, 1'000'000.0, 9'600'000.0),
                                utm(31, false, 0.0, 1'000'000.0), utm(31, false, 1'000'000.0, 10'000'000.0)}) {
    EXPECT_TRUE(ics::frames::from_utm(point, Meters(0.0)).has_value()) << point.zone;
  }
}

TEST(Utm, KeepsTheHeight) {
  const ics::Result<Geodetic> result = ics::frames::from_utm(utm(31, true, 500'000.0, 0.0), Meters(-12.5));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->height(), Meters(-12.5));
}

TEST(Utm, PutsTheFalseOriginOnTheCentralMeridianAtTheEquator) {
  for (const UtmPoint& point : {utm(31, true, 500'000.0, 0.0), utm(31, false, 500'000.0, 10'000'000.0)}) {
    const Geodetic origin = convert(point);
    EXPECT_NEAR(origin.latitude().value(), 0.0, 1e-15);
    EXPECT_NEAR(origin.longitude().value(), 3.0, 1e-15);
  }
}

TEST(Utm, MirrorsTheHemispheresAndTheSidesOfTheZone) {
  const Geodetic north = convert(utm(31, true, 400'000.0, 4'000'000.0));
  const Geodetic south = convert(utm(31, false, 400'000.0, 6'000'000.0));
  const Geodetic east = convert(utm(31, true, 600'000.0, 4'000'000.0));
  EXPECT_NEAR(south.latitude().value(), -north.latitude().value(), 1e-12);
  EXPECT_NEAR(south.longitude().value(), north.longitude().value(), 1e-12);
  EXPECT_NEAR(east.latitude().value(), north.latitude().value(), 1e-12);
  EXPECT_NEAR(east.longitude().value() - 3.0, 3.0 - north.longitude().value(), 1e-12);
}

TEST(Utm, WrapsLongitudesAcrossTheAntimeridian) {
  // 400 km west of zone 1's central meridian, -177, at the equator is about
  // 3.6 degrees, past -180; 400 km east of zone 60's, 177, is past 180.
  EXPECT_NEAR(convert(utm(1, true, 100'000.0, 0.0)).longitude().value(), 179.4, 0.05);
  EXPECT_NEAR(convert(utm(60, true, 900'000.0, 0.0)).longitude().value(), -179.4, 0.05);
}

TEST(Utm, AllocatesNothing) {
  const ics::testing::NoAllocationScope no_allocation;
  const ics::Result<Geodetic> result = ics::frames::from_utm(utm(30, true, 500'000.0, 5'650'000.0), Meters(0.0));
  ASSERT_TRUE(result.has_value());
  EXPECT_NEAR(result->latitude().value(), 51.0, 0.01);
}

}  // namespace
