#include "ics/sapient/location.hpp"

#include <limits>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/geodetic.hpp"

namespace ics::sapient {
namespace {

namespace bsi = sapient_msg::bsi_flex_335_v2_0;

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

const frames::Egm96& geoid() {
  static const Result<frames::Egm96> grid = frames::Egm96::load(ICS_EGM96_PATH);
  EXPECT_TRUE(grid.has_value()) << ICS_EGM96_PATH;
  return *grid;
}

Location degrees(const double latitude, const double longitude) {
  Location location;
  location.set_x(longitude);
  location.set_y(latitude);
  location.set_coordinate_system(bsi::LOCATION_COORDINATE_SYSTEM_LAT_LNG_DEG_M);
  location.set_datum(bsi::LOCATION_DATUM_WGS84_E);
  return location;
}

Location utm(const double easting, const double northing, const std::string& zone) {
  Location location;
  location.set_x(easting);
  location.set_y(northing);
  location.set_coordinate_system(bsi::LOCATION_COORDINATE_SYSTEM_UTM_M);
  location.set_datum(bsi::LOCATION_DATUM_WGS84_E);
  if (!zone.empty()) {
    location.set_utm_zone(zone);
  }
  return location;
}

Fix convert(const Location& location, const std::optional<UtmZone>& default_zone = std::nullopt) {
  const Result<Fix> fix = to_fix(location, default_zone, geoid());
  EXPECT_TRUE(fix.has_value()) << location.x() << " " << location.y();
  return fix.value_or(Fix{.point = frames::Geodetic::make(Degrees(0), Degrees(0), Meters(0)).value(),
                          .has_height = false,
                          .horizontal_sigma_m = std::nullopt,
                          .vertical_sigma_m = std::nullopt});
}

bool rejects(const Location& location, const std::optional<UtmZone>& default_zone = std::nullopt) {
  const Result<Fix> fix = to_fix(location, default_zone, geoid());
  return !fix.has_value() && fix.error() == Error::kInvalidArgument;
}

TEST(SapientZone, ReadsAZoneAndBand) {
  EXPECT_EQ(parse_zone("30U"), (UtmZone{.number = 30, .band = 'U'}));
  EXPECT_EQ(parse_zone("1C"), (UtmZone{.number = 1, .band = 'C'}));
  EXPECT_EQ(parse_zone("60X"), (UtmZone{.number = 60, .band = 'X'}));
  EXPECT_EQ(parse_zone("13s"), (UtmZone{.number = 13, .band = 'S'}));
  EXPECT_EQ(parse_zone("05N"), (UtmZone{.number = 5, .band = 'N'}));
  EXPECT_NE(parse_zone("30U"), (UtmZone{.number = 31, .band = 'U'}));
  EXPECT_NE(parse_zone("30U"), (UtmZone{.number = 30, .band = 'T'}));
}

TEST(SapientZone, RejectsAnythingElse) {
  for (const std::string text : {"", "3", "30", "0N", "61N", "30I", "30O", "30A", "30B", "30Y", "30Z", "U30", "3xU",
                                 "300N", "+3N", "-3N", "30U ", " 3U"}) {
    const Result<UtmZone> zone = parse_zone(text);
    ASSERT_FALSE(zone.has_value()) << text;
    EXPECT_EQ(zone.error(), Error::kInvalidArgument);
  }
}

TEST(SapientLocation, ConvertsDegreesAndTheirErrors) {
  Location location = degrees(40.001, -99.999);
  location.set_z(735.0);
  location.set_x_error(0.00001);
  location.set_y_error(0.00001);
  location.set_z_error(5.0);
  const Fix fix = convert(location);
  EXPECT_EQ(fix.point.latitude().value(), 40.001);
  EXPECT_EQ(fix.point.longitude().value(), -99.999);
  EXPECT_EQ(fix.point.height().value(), 735.0);
  EXPECT_TRUE(fix.has_height);
  // GeographicLib's GeodSolve: 1e-5 degrees of latitude there is 1.110346520
  // m, and of longitude 0.853926112 m, so the latitude's error is larger.
  ASSERT_TRUE(fix.horizontal_sigma_m.has_value());
  EXPECT_NEAR(*fix.horizontal_sigma_m, 1.110346520, 1e-6);
  location.set_x_error(0.00002);
  EXPECT_NEAR(*convert(location).horizontal_sigma_m, 2 * 0.853926112, 1e-6);
  EXPECT_EQ(fix.vertical_sigma_m, 5.0);
}

TEST(SapientLocation, ConvertsRadians) {
  Location location = degrees(0.6982, -1.7452);
  location.set_coordinate_system(bsi::LOCATION_COORDINATE_SYSTEM_LAT_LNG_RAD_M);
  location.set_x_error(1e-7);
  location.set_y_error(1e-7);
  const Fix fix = convert(location);
  EXPECT_NEAR(fix.point.latitude().value(), 40.00391325603408, 1e-12);
  EXPECT_NEAR(fix.point.longitude().value(), -99.99259440623128, 1e-12);
  // 1e-7 radians of latitude at 40 degrees, from the 1e-5 degrees above.
  EXPECT_NEAR(*fix.horizontal_sigma_m, 1.110346520 * 1e-7 / (1e-5 * 3.141592653589793 / 180.0), 1e-4);
}

TEST(SapientLocation, ConvertsUtmInItsOwnZoneOrTheNodes) {
  // golden/frames/utm-geodetic.csv, range-origin-area.
  const Location location = utm(414639.53, 4428236.06, "");
  const Fix fix = convert(location, UtmZone{.number = 14, .band = 'S'});
  EXPECT_NEAR(fix.point.latitude().value(), 39.99999995743866, 1e-12);
  EXPECT_NEAR(fix.point.longitude().value(), -100.00000009493952, 1e-12);
  EXPECT_TRUE(rejects(location));
  // An empty zone is no zone.
  Location empty_zone = location;
  empty_zone.set_utm_zone("");
  EXPECT_EQ(convert(empty_zone, UtmZone{.number = 14, .band = 'S'}).point.latitude(), fix.point.latitude());
  // The location's own zone wins, even when it is not usable.
  EXPECT_EQ(convert(utm(414639.53, 4428236.06, "14S"), UtmZone{.number = 13, .band = 'S'})
                .point.longitude()
                .value(),
            fix.point.longitude().value());
  EXPECT_TRUE(rejects(utm(414639.53, 4428236.06, "14"), UtmZone{.number = 14, .band = 'S'}));
}

TEST(SapientLocation, TakesUtmErrorsAsMetres) {
  Location location = utm(414639.53, 4428236.06, "14S");
  location.set_x_error(3.0);
  location.set_y_error(4.0);
  EXPECT_EQ(convert(location).horizontal_sigma_m, 4.0);
}

TEST(SapientLocation, ChecksTheUtmLatitudeBand) {
  // golden/frames/utm-geodetic.csv, southern-hemisphere: 33.86 S.
  EXPECT_NEAR(convert(utm(334786.9, 6252000.5, "56H")).point.latitude().value(), -33.85938072865515, 1e-12);
  // "56S" read as band S, 32 to 40 N, puts it at 56 N, so it is refused.
  EXPECT_TRUE(rejects(utm(334786.9, 6252000.5, "56S")));
  // 51 N: band U, or N for the northern hemisphere, but not T (40 to 48 N).
  EXPECT_NEAR(convert(utm(500000.0, 5650000.0, "30U")).point.latitude().value(), 51.00157469219964, 1e-12);
  EXPECT_NEAR(convert(utm(500000.0, 5650000.0, "30N")).point.latitude().value(), 51.00157469219964, 1e-12);
  EXPECT_TRUE(rejects(utm(500000.0, 5650000.0, "30T")));
  // GeoConvert: 72.5 N, 80 N, 83.5 N and 79 S at 15 E. Band X runs from 72 N
  // to 84 N, and W from 64 N to 72 N, taken to 73 N.
  EXPECT_NEAR(convert(utm(500000.0, 8044704.7408, "33W")).point.latitude().value(), 72.5, 1e-9);
  EXPECT_NEAR(convert(utm(500000.0, 8044704.7408, "33X")).point.latitude().value(), 72.5, 1e-9);
  EXPECT_NEAR(convert(utm(500000.0, 8881585.8160, "33X")).point.latitude().value(), 80.0, 1e-9);
  EXPECT_TRUE(rejects(utm(500000.0, 8881585.8160, "33W")));
  EXPECT_NEAR(convert(utm(500000.0, 9272275.8710, "33X")).point.latitude().value(), 83.5, 1e-9);
  EXPECT_NEAR(convert(utm(500000.0, 1230025.9862, "33C")).point.latitude().value(), -79.0, 1e-9);
  EXPECT_TRUE(rejects(utm(500000.0, 1230025.9862, "33D")));
}

TEST(SapientLocation, RejectsUtmOutsideItsZone) {
  EXPECT_TRUE(rejects(utm(-1.0, 4428236.06, "14S")));
  EXPECT_TRUE(rejects(utm(kNan, 4428236.06, "14S")));
}

TEST(SapientLocation, RaisesGeoidHeightsToTheEllipsoid) {
  Location location = degrees(40.001, -99.999);
  location.set_datum(bsi::LOCATION_DATUM_WGS84_G);
  location.set_z(1000.0);
  const Fix fix = convert(location);
  // GeographicLib's GeoidEval gives the geoid there as -25.0529 m.
  EXPECT_NEAR(fix.point.height().value(), 1000.0 - 25.0529, 1e-4);
  EXPECT_TRUE(fix.has_height);
}

TEST(SapientLocation, LeavesAnUnknownHeightAndItsErrorUnset) {
  Location location = degrees(40.001, -99.999);
  location.set_z_error(5.0);
  location.set_x_error(1e-5);
  for (const bool with_z : {false, true}) {
    location.set_datum(with_z ? bsi::LOCATION_DATUM_UNSPECIFIED : bsi::LOCATION_DATUM_WGS84_E);
    if (with_z) {
      location.set_z(700.0);
    }
    const Fix fix = convert(location);
    EXPECT_FALSE(fix.has_height);
    EXPECT_EQ(fix.point.height().value(), 0.0);
    EXPECT_FALSE(fix.vertical_sigma_m.has_value());
    EXPECT_FALSE(fix.horizontal_sigma_m.has_value()) << "x_error alone";
  }
  location.set_datum(bsi::LOCATION_DATUM_WGS84_E);
  location.set_z(kNan);
  EXPECT_FALSE(convert(location).has_height);
}

TEST(SapientLocation, LeavesErrorsThatAreNotUsableUnset) {
  for (const double error : {-1.0, kNan, std::numeric_limits<double>::infinity()}) {
    Location location = degrees(40.001, -99.999);
    location.set_z(700.0);
    location.set_x_error(error);
    location.set_y_error(1e-5);
    location.set_z_error(error);
    const Fix fix = convert(location);
    EXPECT_FALSE(fix.horizontal_sigma_m.has_value()) << error;
    EXPECT_FALSE(fix.vertical_sigma_m.has_value()) << error;
  }
}

TEST(SapientLocation, RejectsLocationsThatAreNotPoints) {
  Location no_x = degrees(40.0, -100.0);
  no_x.clear_x();
  Location no_y = degrees(40.0, -100.0);
  no_y.clear_y();
  Location unspecified = degrees(40.0, -100.0);
  unspecified.set_coordinate_system(bsi::LOCATION_COORDINATE_SYSTEM_UNSPECIFIED);
  Location retired = degrees(40.0, -100.0);
  retired.set_coordinate_system(static_cast<bsi::LocationCoordinateSystem>(3));
  for (const Location& location : {no_x, no_y, unspecified, retired, degrees(90.5, 0.0), degrees(kNan, 0.0),
                                   degrees(0.0, std::numeric_limits<double>::infinity())}) {
    EXPECT_TRUE(rejects(location)) << location.x() << " " << location.y();
  }
}

}  // namespace
}  // namespace ics::sapient
