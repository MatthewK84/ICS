#include "ics/cot/adapter.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/cot/event.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"

namespace ics::cot {
namespace {

using std::chrono::seconds;

const UtcTime kNoon = utc_from_ns(1'791'115'200'000'000'000);

frames::Geodetic point(const double latitude, const double longitude, const double height) {
  return frames::Geodetic::make(Degrees(latitude), Degrees(longitude), Meters(height)).value();
}

Event atom() {
  return Event{.uid = "U-1",
               .type = "a-f-A-M-F-Q",
               .how = "m-g",
               .time = kNoon,
               .point = Point{.latitude_deg = 40.0, .longitude_deg = -100.0, .hae_m = 735.0, .ce_m = 2.0, .le_m = 3.0},
               .track = std::nullopt};
}

// An adapter whose range origin is at the atom, so its axes are the atom's.
Adapter adapter(AdapterSettings settings = {}) {
  return Adapter::make(std::move(settings), frames::EnuFrame(point(40.0, -100.0, 735.0))).value();
}

TEST(CotAdapter, RejectsBadSettings) {
  const frames::EnuFrame range(point(40.0, -100.0, 0.0));
  EXPECT_EQ(Adapter::make({.ce_probability = 1.0}, range).error(), Error::kInvalidArgument);
  EXPECT_EQ(Adapter::make({.le_probability = 0.0}, range).error(), Error::kInvalidArgument);
  EXPECT_EQ(Adapter::make({.max_skew = seconds(-1)}, range).error(), Error::kInvalidArgument);
}

TEST(CotAdapter, MakesARecordFromAnAtom) {
  Event event = atom();
  event.track = Track{.course_deg = 30.0, .speed_mps = 10.0};
  const std::optional<v1::PliRecord> record =
      adapter({.roles = {{.uid = "U-1", .role = v1::ENTITY_ROLE_TARGET}}}).record(event, kNoon + seconds(1));
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->entity_id(), "U-1");
  EXPECT_EQ(record->role(), v1::ENTITY_ROLE_TARGET);
  EXPECT_EQ(record->source(), v1::PLI_SOURCE_COT);
  EXPECT_EQ(record->valid_utc_ns(), to_utc_ns(kNoon));
  EXPECT_EQ(record->received_utc_ns(), to_utc_ns(kNoon + seconds(1)));
  EXPECT_EQ(record->time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(record->position().latitude_deg(), 40.0);
  EXPECT_EQ(record->position().longitude_deg(), -100.0);
  EXPECT_EQ(record->position().height_ellipsoid_m(), 735.0);
  EXPECT_NEAR(record->velocity_enu_mps().east(), 5.0, 1e-9);
  EXPECT_NEAR(record->velocity_enu_mps().north(), 10.0 * std::sqrt(3.0) / 2.0, 1e-9);
  EXPECT_NEAR(record->velocity_enu_mps().up(), 0.0, 1e-9);
  EXPECT_NEAR(record->horizontal_sigma_m(), 2.0 / 2.145966026289347, 1e-12);
  EXPECT_NEAR(record->vertical_sigma_m(), 3.0 / 1.6448536269514715, 1e-12);
  EXPECT_EQ(record->fix_type(), v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  EXPECT_FALSE(record->has_attitude());
}

TEST(CotAdapter, UsesTheProbabilitiesItIsGiven) {
  const std::optional<v1::PliRecord> record =
      adapter({.ce_probability = 0.95, .le_probability = 0.95}).record(atom(), kNoon);
  ASSERT_TRUE(record.has_value());
  EXPECT_NEAR(record->horizontal_sigma_m(), 2.0 / 2.4477468306808166, 1e-12);
  EXPECT_NEAR(record->vertical_sigma_m(), 3.0 / 1.9599639845400536, 1e-12);
}

TEST(CotAdapter, RotatesTheVelocityIntoTheRangeFrame) {
  // East at 0 N, 0 E is ECEF Y, which is up at 0 N, 90 E.
  Event event = atom();
  event.point = Point{.latitude_deg = 0.0, .longitude_deg = 0.0, .hae_m = 0.0, .ce_m = 1.0, .le_m = 1.0};
  event.track = Track{.course_deg = 90.0, .speed_mps = 2.0};
  const Adapter far = Adapter::make({}, frames::EnuFrame(point(0.0, 90.0, 0.0))).value();
  const std::optional<v1::PliRecord> record = far.record(event, kNoon);
  ASSERT_TRUE(record.has_value());
  EXPECT_NEAR(record->velocity_enu_mps().east(), 0.0, 1e-12);
  EXPECT_NEAR(record->velocity_enu_mps().north(), 0.0, 1e-12);
  EXPECT_NEAR(record->velocity_enu_mps().up(), 2.0, 1e-12);
}

TEST(CotAdapter, FlagsAReceiptTimeWhenTheSendersTimeIsMissingOrSkewed) {
  const Adapter cot = adapter();
  Event late = atom();
  Event early = atom();
  Event untimed = atom();
  untimed.time = std::nullopt;
  for (const auto& [event, received] : {std::pair{late, kNoon + seconds(31)}, std::pair{early, kNoon - seconds(31)},
                                        std::pair{untimed, kNoon}}) {
    const std::optional<v1::PliRecord> record = cot.record(event, received);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->time_basis(), v1::PLI_TIME_BASIS_RECEIPT);
    EXPECT_EQ(record->valid_utc_ns(), to_utc_ns(received));
  }
  EXPECT_EQ(cot.record(atom(), kNoon - seconds(30))->time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
}

TEST(CotAdapter, LeavesUnknownErrorsAndHeightUnset) {
  Event event = atom();
  event.how = "m-g-n";
  event.point.hae_m = kUnknown;
  event.point.ce_m = kUnknown;
  const std::optional<v1::PliRecord> record = adapter().record(event, kNoon);
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->position().height_ellipsoid_m(), 0.0);
  EXPECT_EQ(record->fix_type(), v1::PliRecord::FIX_TYPE_TWO_DIMENSIONAL);
  EXPECT_FALSE(record->has_horizontal_sigma_m());
  EXPECT_FALSE(record->has_vertical_sigma_m());
  EXPECT_FALSE(record->has_velocity_enu_mps());
}

TEST(CotAdapter, GivesAnythingButAMachineGpsPositionFixTypeOther) {
  for (const std::string how : {"h-e", "m-f", "m-r", "", "m"}) {
    Event event = atom();
    event.how = how;
    EXPECT_EQ(adapter().record(event, kNoon)->fix_type(), v1::PliRecord::FIX_TYPE_OTHER) << how;
  }
  EXPECT_EQ(adapter().record(atom(), kNoon)->role(), v1::ENTITY_ROLE_OTHER);
}

TEST(CotAdapter, GivesNothingForAnEventThatIsNotAPosition) {
  for (const std::string type : {"b-t-f", "t-x-d-d", "b-m-p-s-p-i", "", "a"}) {
    Event event = atom();
    event.type = type;
    EXPECT_EQ(adapter().record(event, kNoon), std::nullopt) << type;
  }
  // A point read_events would have refused.
  Event outside = atom();
  outside.point.latitude_deg = 100.0;
  EXPECT_EQ(adapter().record(outside, kNoon), std::nullopt);
}

}  // namespace
}  // namespace ics::cot
