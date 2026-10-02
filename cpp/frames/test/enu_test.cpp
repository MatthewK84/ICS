#include "ics/frames/enu.hpp"

#include <cmath>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/testing/no_allocation_scope.hpp"

namespace ics::unit {
// A unit for a velocity, to show vectors of any unit rotate.
struct MeterPerSecond;
}  // namespace ics::unit

namespace {

using ics::Degrees;
using ics::Meters;
using ics::frames::Ecef;
using ics::frames::EcefVector;
using ics::frames::Enu;
using ics::frames::EnuFrame;
using ics::frames::EnuVector;
using ics::frames::Geodetic;
using Speed = ics::Quantity<ics::unit::MeterPerSecond>;

Geodetic point(const double latitude, const double longitude, const double height) {
  return Geodetic::make(Degrees(latitude), Degrees(longitude), Meters(height)).value();
}

double length(const Enu& v) { return std::hypot(v.east.value(), v.north.value(), v.up.value()); }

TEST(EnuFrame, PutsTheOriginAtZero) {
  const EnuFrame frame(point(40.0, -100.0, 700.0));
  EXPECT_EQ(frame.origin().latitude(), Degrees(40.0));
  const Enu origin = frame.to_enu(point(40.0, -100.0, 700.0));
  EXPECT_EQ(origin.east, Meters(0.0));
  EXPECT_EQ(origin.north, Meters(0.0));
  EXPECT_EQ(origin.up, Meters(0.0));
}

TEST(EnuFrame, PointsItsAxesEastNorthAndUp) {
  // At latitude 0 and longitude 0, ECEF x is up, y is east and z is north.
  const EnuFrame frame(point(0.0, 0.0, 0.0));
  const Enu up = frame.rotate(Ecef{Meters(1.0), Meters(0.0), Meters(0.0)});
  const Enu east = frame.rotate(Ecef{Meters(0.0), Meters(1.0), Meters(0.0)});
  const Enu north = frame.rotate(Ecef{Meters(0.0), Meters(0.0), Meters(1.0)});
  EXPECT_EQ(up.up, Meters(1.0));
  EXPECT_EQ(east.east, Meters(1.0));
  EXPECT_EQ(north.north, Meters(1.0));
  EXPECT_EQ(length(up) + length(east) + length(north), 3.0);
}

TEST(EnuFrame, RoundTripsPositions) {
  const EnuFrame frame(point(-33.9, 151.2, 50.0));
  for (const Enu& position : {Enu{Meters(1'000.0), Meters(-2'000.0), Meters(300.0)},
                              Enu{Meters(-50'000.0), Meters(80'000.0), Meters(-500.0)}}) {
    const Ecef ecef = frame.to_ecef(position);
    const Enu back = frame.to_enu(ecef);
    EXPECT_NEAR(back.east.value(), position.east.value(), 1e-6);
    EXPECT_NEAR(back.north.value(), position.north.value(), 1e-6);
    EXPECT_NEAR(back.up.value(), position.up.value(), 1e-6);
    const ics::Result<Geodetic> geodetic = frame.to_geodetic(position);
    ASSERT_TRUE(geodetic.has_value());
    const Enu again = frame.to_enu(*geodetic);
    EXPECT_NEAR(again.east.value(), position.east.value(), 1e-6);
    EXPECT_NEAR(again.north.value(), position.north.value(), 1e-6);
    EXPECT_NEAR(again.up.value(), position.up.value(), 1e-6);
  }
}

TEST(EnuFrame, RotatesVectorsOfAnyUnitWithoutMovingThem) {
  const EnuFrame frame(point(40.0, -100.0, 700.0));
  const EcefVector<ics::unit::MeterPerSecond> velocity{Speed(3.0), Speed(-4.0), Speed(12.0)};
  const EnuVector<ics::unit::MeterPerSecond> enu = frame.rotate(velocity);
  EXPECT_NEAR(std::hypot(enu.east.value(), enu.north.value(), enu.up.value()), 13.0, 1e-12);
  const EcefVector<ics::unit::MeterPerSecond> back = frame.rotate(enu);
  EXPECT_NEAR(back.x.value(), 3.0, 1e-12);
  EXPECT_NEAR(back.y.value(), -4.0, 1e-12);
  EXPECT_NEAR(back.z.value(), 12.0, 1e-12);
}

TEST(EnuFrame, RejectsAPositionTooFarToRepresent) {
  const EnuFrame frame(point(0.0, 0.0, 0.0));
  const ics::Result<Geodetic> far = frame.to_geodetic(Enu{Meters(1.5e308), Meters(1.5e308), Meters(0.0)});
  ASSERT_FALSE(far.has_value());
  EXPECT_EQ(far.error(), ics::Error::kInvalidArgument);
}

TEST(EnuFrame, AllocatesNothing) {
  const EnuFrame frame(point(40.0, -100.0, 700.0));
  const Geodetic target = point(40.18, -100.0, 700.0);
  const ics::testing::NoAllocationScope no_allocation;
  const Enu north = frame.to_enu(target);
  EXPECT_GT(north.north.value(), 19'000.0);
  EXPECT_TRUE(frame.to_geodetic(north).has_value());
}

}  // namespace
