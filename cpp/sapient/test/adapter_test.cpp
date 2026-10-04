#include "ics/sapient/adapter.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"

namespace ics::sapient {
namespace {

namespace bsi = sapient_msg::bsi_flex_335_v2_0;
using std::chrono::seconds;

// 2026-10-04T12:00:00Z.
constexpr std::int64_t kNoonSeconds = 1'791'115'200;
const UtcTime kNoon = utc_from_ns(kNoonSeconds * 1'000'000'000);
const std::string kNode = "3f2a9c1e-5b7d-4e8f-9a01-2c3d4e5f6a7b";

const frames::Egm96& geoid() {
  static const Result<frames::Egm96> grid = frames::Egm96::load(ICS_EGM96_PATH);
  EXPECT_TRUE(grid.has_value()) << ICS_EGM96_PATH;
  return *grid;
}

frames::Geodetic point(const double latitude, const double longitude) {
  return frames::Geodetic::make(Degrees(latitude), Degrees(longitude), Meters(0.0)).value();
}

// An adapter whose range origin is at the UTM detection below.
Adapter adapter(AdapterSettings settings = {}) {
  return Adapter::make(std::move(settings), geoid(), frames::EnuFrame(point(39.99999995743866, -100.00000009493952)))
      .value();
}

Message registration(const std::string& node_id, const std::string& zone) {
  Message message;
  message.set_node_id(node_id);
  Registration::DetectionDefinition& definition =
      *message.mutable_registration()->add_mode_definition()->add_detection_definition();
  definition.mutable_location_type()->set_location_units(bsi::LOCATION_COORDINATE_SYSTEM_UTM_M);
  definition.mutable_location_type()->set_zone(zone);
  bsi::ENUVelocityUnits& units = *definition.mutable_velocity_type()->mutable_enu_velocity_units();
  units.set_east_north_rate_units(bsi::SPEED_UNITS_KPH);
  units.set_up_rate_units(bsi::SPEED_UNITS_MS);
  return message;
}

// A detection at golden/frames/utm-geodetic.csv's range-origin-area, in the
// node's zone, moving east at 36 km/h and up at 1 m/s.
Message detection(const std::string& node_id = kNode) {
  Message message;
  message.set_node_id(node_id);
  message.mutable_timestamp()->set_seconds(kNoonSeconds);
  bsi::DetectionReport& report = *message.mutable_detection_report();
  report.set_object_id("01K6PZ0000000000000000B001");
  report.set_id("N123AB");
  bsi::Location& location = *report.mutable_location();
  location.set_x(414639.53);
  location.set_y(4428236.06);
  location.set_z(735.0);
  location.set_x_error(3.0);
  location.set_y_error(4.0);
  location.set_z_error(5.0);
  location.set_coordinate_system(bsi::LOCATION_COORDINATE_SYSTEM_UTM_M);
  location.set_datum(bsi::LOCATION_DATUM_WGS84_E);
  report.mutable_enu_velocity()->set_east_rate(36.0);
  report.mutable_enu_velocity()->set_north_rate(0.0);
  report.mutable_enu_velocity()->set_up_rate(1.0);
  return message;
}

TEST(SapientAdapter, RejectsANegativeMaxSkew) {
  EXPECT_EQ(Adapter::make({.max_skew = seconds(-1)}, geoid(), frames::EnuFrame(point(40.0, -100.0))).error(),
            Error::kInvalidArgument);
}

TEST(SapientAdapter, MakesARecordFromADetection) {
  Adapter sapient = adapter({.roles = {{.id = "N123AB", .role = v1::ENTITY_ROLE_TARGET}}});
  EXPECT_EQ(sapient.receive(registration(kNode, "14S"), kNoon), std::nullopt);
  const std::optional<v1::PliRecord> record = sapient.receive(detection(), kNoon + seconds(1));
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->entity_id(), "01K6PZ0000000000000000B001");
  EXPECT_EQ(record->role(), v1::ENTITY_ROLE_TARGET);
  EXPECT_EQ(record->source(), v1::PLI_SOURCE_SAPIENT);
  EXPECT_EQ(record->valid_utc_ns(), to_utc_ns(kNoon));
  EXPECT_EQ(record->received_utc_ns(), to_utc_ns(kNoon + seconds(1)));
  EXPECT_EQ(record->time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_NEAR(record->position().latitude_deg(), 39.99999995743866, 1e-12);
  EXPECT_NEAR(record->position().longitude_deg(), -100.00000009493952, 1e-12);
  EXPECT_EQ(record->position().height_ellipsoid_m(), 735.0);
  EXPECT_NEAR(record->velocity_enu_mps().east(), 10.0, 1e-9);
  EXPECT_NEAR(record->velocity_enu_mps().north(), 0.0, 1e-9);
  EXPECT_NEAR(record->velocity_enu_mps().up(), 1.0, 1e-9);
  EXPECT_EQ(record->horizontal_sigma_m(), 4.0);
  EXPECT_EQ(record->vertical_sigma_m(), 5.0);
  EXPECT_EQ(record->fix_type(), v1::PliRecord::FIX_TYPE_OTHER);
  EXPECT_FALSE(record->has_attitude());
}

TEST(SapientAdapter, RotatesTheVelocityIntoTheRangeFrame) {
  // East at 0 N, 0 E is ECEF Y, which is up at 0 N, 90 E.
  Adapter far = Adapter::make({}, geoid(), frames::EnuFrame(point(0.0, 90.0))).value();
  static_cast<void>(far.receive(registration(kNode, "31N"), kNoon));
  Message message = detection();
  bsi::Location& location = *message.mutable_detection_report()->mutable_location();
  location.set_coordinate_system(bsi::LOCATION_COORDINATE_SYSTEM_LAT_LNG_DEG_M);
  location.set_x(0.0);
  location.set_y(0.0);
  message.mutable_detection_report()->mutable_enu_velocity()->set_up_rate(0.0);
  const std::optional<v1::PliRecord> record = far.receive(message, kNoon);
  ASSERT_TRUE(record.has_value());
  EXPECT_NEAR(record->velocity_enu_mps().east(), 0.0, 1e-12);
  EXPECT_NEAR(record->velocity_enu_mps().north(), 0.0, 1e-12);
  EXPECT_NEAR(record->velocity_enu_mps().up(), 10.0, 1e-12);
}

TEST(SapientAdapter, LeavesTheVelocityUnsetWithoutUnitsOrRates) {
  Message message = detection();
  message.mutable_detection_report()->mutable_location()->set_utm_zone("14S");
  // No registration, so no units.
  EXPECT_FALSE(adapter().receive(message, kNoon)->has_velocity_enu_mps());

  Adapter registered = adapter();
  static_cast<void>(registered.receive(registration(kNode, "14S"), kNoon));
  Message no_north = detection();
  no_north.mutable_detection_report()->mutable_enu_velocity()->clear_north_rate();
  Message no_east = detection();
  no_east.mutable_detection_report()->mutable_enu_velocity()->clear_east_rate();
  Message infinite_east = detection();
  infinite_east.mutable_detection_report()->mutable_enu_velocity()->set_east_rate(
      std::numeric_limits<double>::infinity());
  Message nan_north = detection();
  nan_north.mutable_detection_report()->mutable_enu_velocity()->set_north_rate(
      std::numeric_limits<double>::quiet_NaN());
  Message infinite_up = detection();
  infinite_up.mutable_detection_report()->mutable_enu_velocity()->set_up_rate(
      std::numeric_limits<double>::infinity());
  Message none = detection();
  none.mutable_detection_report()->clear_enu_velocity();
  for (const Message& unusable : {no_north, no_east, infinite_east, nan_north, infinite_up, none}) {
    const std::optional<v1::PliRecord> record = registered.receive(unusable, kNoon);
    ASSERT_TRUE(record.has_value());
    EXPECT_FALSE(record->has_velocity_enu_mps());
  }
  Message no_up = detection();
  no_up.mutable_detection_report()->mutable_enu_velocity()->clear_up_rate();
  EXPECT_NEAR(registered.receive(no_up, kNoon)->velocity_enu_mps().up(), 0.0, 1e-9);
}

TEST(SapientAdapter, CountsTheDetectionsItCannotPlace) {
  Adapter sapient = adapter();
  Message range_bearing = detection();
  range_bearing.mutable_detection_report()->mutable_range_bearing()->set_range(1500.0);
  Message unidentified = detection();
  unidentified.mutable_detection_report()->clear_object_id();
  Message no_location = detection();
  no_location.mutable_detection_report()->clear_location();
  // No registration, so no zone for the UTM location.
  for (const Message& message : {range_bearing, unidentified, no_location, detection()}) {
    EXPECT_EQ(sapient.receive(message, kNoon), std::nullopt);
  }
  EXPECT_EQ(sapient.counts().range_bearing, 1U);
  EXPECT_EQ(sapient.counts().unidentified, 1U);
  EXPECT_EQ(sapient.counts().unlocated, 2U);
}

TEST(SapientAdapter, TakesTheReceiptTimeWhenTheTimestampIsMissingOrSkewed) {
  Adapter sapient = adapter();
  static_cast<void>(sapient.receive(registration(kNode, "14S"), kNoon));
  Message untimed = detection();
  untimed.clear_timestamp();
  Message before_1970 = detection();
  before_1970.mutable_timestamp()->set_seconds(-1);
  Message after_2200 = detection();
  after_2200.mutable_timestamp()->set_seconds(7'289'654'400);
  Message bad_nanos = detection();
  bad_nanos.mutable_timestamp()->set_nanos(1'000'000'000);
  Message negative_nanos = detection();
  negative_nanos.mutable_timestamp()->set_nanos(-1);
  for (const auto& [message, received] :
       {std::pair{detection(), kNoon + seconds(31)}, std::pair{detection(), kNoon - seconds(31)},
        std::pair{untimed, kNoon}, std::pair{before_1970, kNoon}, std::pair{after_2200, kNoon},
        std::pair{bad_nanos, kNoon}, std::pair{negative_nanos, kNoon}}) {
    const std::optional<v1::PliRecord> record = sapient.receive(message, received);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->time_basis(), v1::PLI_TIME_BASIS_RECEIPT);
    EXPECT_EQ(record->valid_utc_ns(), to_utc_ns(received));
  }
  Message last = detection();
  last.mutable_timestamp()->set_seconds(7'289'654'399);
  last.mutable_timestamp()->set_nanos(999'999'999);
  const UtcTime at_last = utc_from_ns(7'289'654'399'999'999'999);
  EXPECT_EQ(sapient.receive(last, at_last)->time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(sapient.receive(detection(), kNoon - seconds(30))->time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
}

TEST(SapientAdapter, KeepsTheUnitsOfAsManyNodesAsItIsAllowed) {
  Adapter sapient = adapter({.max_nodes = 1});
  static_cast<void>(sapient.receive(registration(kNode, "13S"), kNoon));
  // A second node is past the limit, but the first may register again.
  static_cast<void>(sapient.receive(registration("other", "14S"), kNoon));
  EXPECT_EQ(sapient.receive(detection("other"), kNoon), std::nullopt);
  // In zone 13, whose central meridian is 105 W, the detection is 85 km west
  // of it; in zone 14, at 100 W.
  EXPECT_LT(sapient.receive(detection(), kNoon)->position().longitude_deg(), -105.0);
  static_cast<void>(sapient.receive(registration(kNode, "14S"), kNoon));
  EXPECT_NEAR(sapient.receive(detection(), kNoon)->position().longitude_deg(), -100.0, 1e-6);
  static_cast<void>(sapient.receive(registration("", "14S"), kNoon));
  static_cast<void>(sapient.receive(registration(std::string(Adapter::kMaxNodeIdBytes + 1, 'n'), "14S"), kNoon));
  EXPECT_EQ(sapient.counts().registrations_ignored, 3U);

  Adapter none = adapter({.max_nodes = 0});
  static_cast<void>(none.receive(registration(kNode, "14S"), kNoon));
  EXPECT_EQ(none.counts().registrations_ignored, 1U);
  EXPECT_EQ(none.receive(detection(), kNoon), std::nullopt);
}

TEST(SapientAdapter, MatchesRolesByObjectIdOrId) {
  Message message = detection();
  message.mutable_detection_report()->mutable_location()->set_utm_zone("14S");
  const AdapterSettings by_object{.roles = {{.id = "01K6PZ0000000000000000B001", .role = v1::ENTITY_ROLE_INTERCEPTOR}}};
  EXPECT_EQ(adapter(by_object).receive(message, kNoon)->role(), v1::ENTITY_ROLE_INTERCEPTOR);
  EXPECT_EQ(adapter().receive(message, kNoon)->role(), v1::ENTITY_ROLE_OTHER);
  const AdapterSettings someone_else{.roles = {{.id = "N999ZZ", .role = v1::ENTITY_ROLE_TARGET}}};
  EXPECT_EQ(adapter(someone_else).receive(message, kNoon)->role(), v1::ENTITY_ROLE_OTHER);
  // A report with no id matches no role, not even one with an empty id.
  message.mutable_detection_report()->clear_id();
  EXPECT_EQ(adapter({.roles = {{.id = "", .role = v1::ENTITY_ROLE_TARGET}}}).receive(message, kNoon)->role(),
            v1::ENTITY_ROLE_OTHER);
}

TEST(SapientAdapter, GivesNothingForOtherMessages) {
  Adapter sapient = adapter();
  Message status;
  status.set_node_id(kNode);
  status.mutable_status_report()->set_report_id("01K6PZ0000000000000000S001");
  EXPECT_EQ(sapient.receive(status, kNoon), std::nullopt);
  EXPECT_EQ(sapient.receive(Message(), kNoon), std::nullopt);
}

}  // namespace
}  // namespace ics::sapient
