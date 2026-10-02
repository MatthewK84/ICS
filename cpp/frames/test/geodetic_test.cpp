#include "ics/frames/geodetic.hpp"

#include <array>
#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/wgs84.hpp"
#include "ics/testing/no_allocation_scope.hpp"

namespace {

using ics::Degrees;
using ics::Meters;
using ics::frames::Ecef;
using ics::frames::Geodetic;

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();
const double kPolarRadius = ics::frames::wgs84::kSemiMinorAxis.value();

Geodetic point(const double latitude, const double longitude, const double height) {
  const ics::Result<Geodetic> made = Geodetic::make(Degrees(latitude), Degrees(longitude), Meters(height));
  EXPECT_TRUE(made.has_value()) << latitude << " " << longitude << " " << height;
  return made.value_or(Geodetic::make(Degrees(0), Degrees(0), Meters(0)).value());
}

Geodetic from_ecef(const double x, const double y, const double z) {
  const ics::Result<Geodetic> result = ics::frames::to_geodetic(Ecef{Meters(x), Meters(y), Meters(z)});
  EXPECT_TRUE(result.has_value()) << x << " " << y << " " << z;
  return result.value_or(point(0, 0, 0));
}

TEST(Geodetic, KeepsValidValues) {
  const Geodetic made = point(40.0, -100.0, 700.0);
  EXPECT_EQ(made.latitude(), Degrees(40.0));
  EXPECT_EQ(made.longitude(), Degrees(-100.0));
  EXPECT_EQ(made.height(), Meters(700.0));
  EXPECT_EQ(point(-90.0, 0.0, 0.0).latitude(), Degrees(-90.0));
}

TEST(Geodetic, WrapsTheLongitude) {
  EXPECT_EQ(point(0.0, 180.0, 0.0).longitude(), Degrees(-180.0));
  EXPECT_EQ(point(0.0, 370.0, 0.0).longitude(), Degrees(10.0));
  EXPECT_EQ(point(0.0, -190.0, 0.0).longitude(), Degrees(170.0));
}

TEST(Geodetic, RejectsValuesThatAreNotFinite) {
  for (const auto& [latitude, longitude, height] : {std::array{kNan, 0.0, 0.0}, std::array{0.0, kInfinity, 0.0},
                                                     std::array{0.0, 0.0, -kInfinity}}) {
    const ics::Result<Geodetic> made = Geodetic::make(Degrees(latitude), Degrees(longitude), Meters(height));
    ASSERT_FALSE(made.has_value());
    EXPECT_EQ(made.error(), ics::Error::kInvalidArgument);
  }
}

TEST(Geodetic, RejectsALatitudeBeyondAPole) {
  for (const double latitude : {90.0000001, -91.0}) {
    const ics::Result<Geodetic> made = Geodetic::make(Degrees(latitude), Degrees(0.0), Meters(0.0));
    ASSERT_FALSE(made.has_value());
    EXPECT_EQ(made.error(), ics::Error::kOutOfRange);
  }
}

TEST(ToEcef, IsExactOnTheAxes) {
  const Ecef pole = ics::frames::to_ecef(point(90.0, 0.0, 0.0));
  EXPECT_EQ(pole.x, Meters(0.0));
  EXPECT_EQ(pole.y, Meters(0.0));
  EXPECT_DOUBLE_EQ(pole.z.value(), kPolarRadius);
  const Ecef east = ics::frames::to_ecef(point(0.0, 90.0, 10.0));
  EXPECT_EQ(east.x, Meters(0.0));
  EXPECT_EQ(east.y, Meters(6'378'147.0));
  EXPECT_EQ(east.z, Meters(0.0));
}

// Every case of the inverse, each checked against the forward conversion.
TEST(ToGeodetic, RoundTripsEverywhereOnAndAboveTheEarth) {
  for (const double latitude : {-90.0, -89.9999, -45.0, -1e-9, 0.0, 33.3, 89.9, 90.0}) {
    for (const double longitude : {-180.0, -100.0, -0.5, 0.0, 45.0, 135.0, 179.999}) {
      for (const double height : {-400.0, 0.0, 700.0, 12'000.0, 36'000'000.0}) {
        const Geodetic original = point(latitude, longitude, height);
        const Ecef ecef = ics::frames::to_ecef(original);
        const Geodetic back = from_ecef(ecef.x.value(), ecef.y.value(), ecef.z.value());
        EXPECT_NEAR(back.latitude().value(), latitude, 1e-11) << latitude << " " << longitude << " " << height;
        EXPECT_NEAR(back.height().value(), height, 1e-6) << latitude << " " << longitude << " " << height;
        if (std::fabs(latitude) < 90.0) {
          EXPECT_NEAR(back.longitude().value(), longitude, 1e-11) << latitude << " " << longitude << " " << height;
        }
      }
    }
  }
}

TEST(ToGeodetic, HandlesThePolarAxisAndTheCentre) {
  const Geodetic centre = from_ecef(0.0, 0.0, 0.0);
  EXPECT_EQ(centre.latitude(), Degrees(90.0));
  EXPECT_NEAR(centre.height().value(), -kPolarRadius, 1e-6);
  const Geodetic south = from_ecef(0.0, 0.0, -kPolarRadius - 5.0);
  EXPECT_EQ(south.latitude(), Degrees(-90.0));
  EXPECT_NEAR(south.height().value(), 5.0, 1e-6);
  // On the axis, near the centre: the cube root's sign is chosen for accuracy.
  const Geodetic inside = from_ecef(0.0, 0.0, 10'000.0);
  EXPECT_EQ(inside.latitude(), Degrees(90.0));
  EXPECT_NEAR(inside.height().value(), 10'000.0 - kPolarRadius, 1e-6);
}

// Points within the ellipsoid's evolute, about 43 km of the centre, take the
// branches that a surface point never does; each must still map back.
TEST(ToGeodetic, RoundTripsNearTheCentre) {
  for (const auto& [r, z] : {std::array{10'000.0, 10'000.0}, std::array{30'000.0, 1'000.0},
                             std::array{20'000.0, 20'000.0}, std::array{1'000.0, -30'000.0}}) {
    const Geodetic inverse = from_ecef(r, 0.0, z);
    const Ecef forward = ics::frames::to_ecef(inverse);
    EXPECT_NEAR(forward.x.value(), r, 1e-6) << r << " " << z;
    EXPECT_NEAR(forward.y.value(), 0.0, 1e-6) << r << " " << z;
    EXPECT_NEAR(forward.z.value(), z, 1e-6) << r << " " << z;
  }
}

TEST(ToGeodetic, HandlesTheEquatorialPlaneInsideTheEllipsoid) {
  const Geodetic north = from_ecef(10'000.0, 0.0, 0.0);
  EXPECT_GT(north.latitude().value(), 0.0);
  const Ecef forward = ics::frames::to_ecef(north);
  EXPECT_NEAR(forward.x.value(), 10'000.0, 1e-6);
  EXPECT_NEAR(forward.z.value(), 0.0, 1e-6);
  // A z so small that it squares to 0 still picks the southern solution.
  const Geodetic south = from_ecef(10'000.0, 0.0, -1e-200);
  EXPECT_EQ(south.latitude(), Degrees(-north.latitude().value()));
}

TEST(ToGeodetic, TreatsAFarPointAsSeenFromAPoint) {
  const Geodetic far = from_ecef(1e30, 0.0, 1e30);
  EXPECT_NEAR(far.latitude().value(), 45.0, 1e-12);
  EXPECT_EQ(far.longitude(), Degrees(0.0));
  EXPECT_NEAR(far.height().value(), std::sqrt(2.0) * 1e30, 1e16);
}

TEST(ToGeodetic, RejectsAPositionThatIsNotFiniteOrTooFar) {
  for (const auto& [x, y, z] : {std::array{kNan, 0.0, 0.0}, std::array{0.0, kInfinity, 0.0},
                                std::array{1.5e308, 1.5e308, 0.0}}) {
    const ics::Result<Geodetic> result = ics::frames::to_geodetic(Ecef{Meters(x), Meters(y), Meters(z)});
    ASSERT_FALSE(result.has_value()) << x << " " << y << " " << z;
    EXPECT_EQ(result.error(), ics::Error::kInvalidArgument);
  }
}

TEST(Conversions, AllocateNothing) {
  const Geodetic origin = point(40.0, -100.0, 700.0);
  const ics::testing::NoAllocationScope no_allocation;
  const Ecef ecef = ics::frames::to_ecef(origin);
  const ics::Result<Geodetic> back = ics::frames::to_geodetic(ecef);
  ASSERT_TRUE(back.has_value());
  EXPECT_NEAR(back->latitude().value(), 40.0, 1e-12);
}

}  // namespace
