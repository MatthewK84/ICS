#include "ics/sapient/registration.hpp"

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "ics/sapient/location.hpp"

namespace ics::sapient {
namespace {

namespace bsi = sapient_msg::bsi_flex_335_v2_0;

constexpr double kKph = 1.0 / 3.6;

// A detection definition in UTM, in a zone (none when empty), with ENU
// velocity units (none when unspecified).
void define(Registration& registration, const std::string& zone, const bsi::SpeedUnits horizontal,
            const std::optional<bsi::SpeedUnits> vertical = std::nullopt) {
  Registration::ModeDefinition& mode = *registration.add_mode_definition();
  Registration::DetectionDefinition& definition = *mode.add_detection_definition();
  definition.mutable_location_type()->set_location_units(bsi::LOCATION_COORDINATE_SYSTEM_UTM_M);
  definition.mutable_location_type()->set_location_datum(bsi::LOCATION_DATUM_WGS84_E);
  if (!zone.empty()) {
    definition.mutable_location_type()->set_zone(zone);
  }
  if (horizontal == bsi::SPEED_UNITS_UNSPECIFIED) {
    return;
  }
  bsi::ENUVelocityUnits& units = *definition.mutable_velocity_type()->mutable_enu_velocity_units();
  units.set_east_north_rate_units(horizontal);
  if (vertical) {
    units.set_up_rate_units(*vertical);
  }
}

TEST(SapientRegistration, ReadsTheZoneAndVelocityUnits) {
  Registration registration;
  define(registration, "14S", bsi::SPEED_UNITS_KPH, bsi::SPEED_UNITS_MS);
  const NodeUnits units = read_units(registration);
  EXPECT_EQ(units.zone, (UtmZone{.number = 14, .band = 'S'}));
  ASSERT_TRUE(units.velocity.has_value());
  EXPECT_DOUBLE_EQ(units.velocity->horizontal_mps, kKph);
  EXPECT_EQ(units.velocity->vertical_mps, 1.0);
}

TEST(SapientRegistration, GivesTheUpRateTheHorizontalUnitsWhenItHasNone) {
  Registration registration;
  define(registration, "", bsi::SPEED_UNITS_KPH);
  const NodeUnits units = read_units(registration);
  EXPECT_FALSE(units.zone.has_value());
  EXPECT_EQ(units.velocity, (VelocityUnits{.horizontal_mps = kKph, .vertical_mps = kKph}));
}

TEST(SapientRegistration, KeepsWhatEveryModeAgreesOn) {
  Registration registration;
  define(registration, "14S", bsi::SPEED_UNITS_MS);
  define(registration, "", bsi::SPEED_UNITS_UNSPECIFIED);
  define(registration, "14s", bsi::SPEED_UNITS_MS, bsi::SPEED_UNITS_MS);
  const NodeUnits units = read_units(registration);
  EXPECT_EQ(units.zone, (UtmZone{.number = 14, .band = 'S'}));
  EXPECT_EQ(units.velocity, (VelocityUnits{.horizontal_mps = 1.0, .vertical_mps = 1.0}));
}

TEST(SapientRegistration, LeavesUnitsTheModesDisagreeOnUnset) {
  Registration registration;
  define(registration, "14S", bsi::SPEED_UNITS_MS);
  define(registration, "13S", bsi::SPEED_UNITS_KPH);
  // A third that agrees with the first does not settle it.
  define(registration, "14S", bsi::SPEED_UNITS_MS);
  const NodeUnits units = read_units(registration);
  EXPECT_FALSE(units.zone.has_value());
  EXPECT_FALSE(units.velocity.has_value());

  Registration vertical;
  define(vertical, "14S", bsi::SPEED_UNITS_MS, bsi::SPEED_UNITS_MS);
  define(vertical, "14S", bsi::SPEED_UNITS_MS, bsi::SPEED_UNITS_KPH);
  EXPECT_FALSE(read_units(vertical).velocity.has_value());
  EXPECT_TRUE(read_units(vertical).zone.has_value());
}

TEST(SapientRegistration, ComparesUnitsByEveryField) {
  EXPECT_NE((VelocityUnits{.horizontal_mps = 1.0, .vertical_mps = 1.0}),
            (VelocityUnits{.horizontal_mps = kKph, .vertical_mps = 1.0}));
  EXPECT_NE((VelocityUnits{.horizontal_mps = 1.0, .vertical_mps = 1.0}),
            (VelocityUnits{.horizontal_mps = 1.0, .vertical_mps = kKph}));
}

TEST(SapientRegistration, LeavesUnitsItCannotReadUnset) {
  Registration bad_zone;
  define(bad_zone, "14S", bsi::SPEED_UNITS_MS);
  define(bad_zone, "14", bsi::SPEED_UNITS_MS);
  EXPECT_FALSE(read_units(bad_zone).zone.has_value());
  EXPECT_TRUE(read_units(bad_zone).velocity.has_value());

  Registration retired_speed;
  define(retired_speed, "14S", bsi::SPEED_UNITS_MS);
  define(retired_speed, "14S", static_cast<bsi::SpeedUnits>(3));
  EXPECT_FALSE(read_units(retired_speed).velocity.has_value());
  EXPECT_TRUE(read_units(retired_speed).zone.has_value());

  Registration unspecified_up;
  define(unspecified_up, "14S", bsi::SPEED_UNITS_MS, bsi::SPEED_UNITS_UNSPECIFIED);
  EXPECT_FALSE(read_units(unspecified_up).velocity.has_value());
}

TEST(SapientRegistration, IgnoresAVelocityTypeWithoutEnuUnits) {
  Registration registration;
  define(registration, "14S", bsi::SPEED_UNITS_KPH);
  Registration::DetectionDefinition& datum_only =
      *registration.mutable_mode_definition(0)->add_detection_definition();
  datum_only.mutable_velocity_type()->set_location_datum(bsi::LOCATION_DATUM_WGS84_E);
  EXPECT_EQ(read_units(registration).velocity, (VelocityUnits{.horizontal_mps = kKph, .vertical_mps = kKph}));
}

TEST(SapientRegistration, IgnoresZonesOfOtherCoordinateSystems) {
  Registration registration;
  define(registration, "14S", bsi::SPEED_UNITS_UNSPECIFIED);
  Registration::DetectionDefinition& degrees =
      *registration.mutable_mode_definition(0)->add_detection_definition();
  degrees.mutable_location_type()->set_location_units(bsi::LOCATION_COORDINATE_SYSTEM_LAT_LNG_DEG_M);
  degrees.mutable_location_type()->set_zone("13S");
  EXPECT_EQ(read_units(registration).zone, (UtmZone{.number = 14, .band = 'S'}));
  EXPECT_FALSE(read_units(Registration()).zone.has_value());
}

}  // namespace
}  // namespace ics::sapient
