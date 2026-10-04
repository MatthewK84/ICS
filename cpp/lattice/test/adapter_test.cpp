#include "ics/lattice/adapter.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/lattice/event.hpp"

namespace ics::lattice {
namespace {

using std::chrono::seconds;

const UtcTime kNoon = utc_from_ns(1'791'115'200'000'000'000);

frames::Geodetic point(const double latitude, const double longitude, const double height) {
  return frames::Geodetic::make(Degrees(latitude), Degrees(longitude), Meters(height)).value();
}

// An UPDATE of entity E-1 at 40 N, 100 W, 735 m, reported at noon.
Event update() {
  return Event{.type = EventType::kUpdate,
               .time = kNoon + seconds(1),
               .entity = Entity{.entity_id = "E-1",
                                .live = true,
                                .position = Position{.latitude_deg = 40.0, .longitude_deg = -100.0, .hae_m = 735.0},
                                .velocity_enu_mps = std::nullopt,
                                .position_covariance_m2 = std::nullopt,
                                .source_update_time = kNoon}};
}

// An adapter whose range origin is at the entity, so its axes are the entity's.
Adapter adapter(AdapterSettings settings = {}) {
  return Adapter::make(std::move(settings), frames::EnuFrame(point(40.0, -100.0, 735.0))).value();
}

TEST(LatticeAdapter, RejectsANegativeSkew) {
  EXPECT_EQ(Adapter::make({.max_skew = seconds(-1)}, frames::EnuFrame(point(0.0, 0.0, 0.0))).error(),
            Error::kInvalidArgument);
}

TEST(LatticeAdapter, MakesARecordFromAnUpdate) {
  Event event = update();
  event.entity.velocity_enu_mps = Enu{.east = 3.0, .north = -4.0, .up = 0.5};
  event.entity.position_covariance_m2 = Covariance{.xx = 9.0, .xy = 0.0, .xz = 1.0, .yy = 4.0, .yz = 1.0, .zz = 16.0};
  const std::optional<v1::PliRecord> record =
      adapter({.roles = {{.entity_id = "E-1", .role = v1::ENTITY_ROLE_TARGET}}}).record(event, kNoon + seconds(2));
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->entity_id(), "E-1");
  EXPECT_EQ(record->role(), v1::ENTITY_ROLE_TARGET);
  EXPECT_EQ(record->source(), v1::PLI_SOURCE_LATTICE);
  EXPECT_EQ(record->valid_utc_ns(), to_utc_ns(kNoon));
  EXPECT_EQ(record->received_utc_ns(), to_utc_ns(kNoon + seconds(2)));
  EXPECT_EQ(record->time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(record->position().latitude_deg(), 40.0);
  EXPECT_EQ(record->position().longitude_deg(), -100.0);
  EXPECT_EQ(record->position().height_ellipsoid_m(), 735.0);
  EXPECT_NEAR(record->velocity_enu_mps().east(), 3.0, 1e-9);
  EXPECT_NEAR(record->velocity_enu_mps().north(), -4.0, 1e-9);
  EXPECT_NEAR(record->velocity_enu_mps().up(), 0.5, 1e-9);
  EXPECT_EQ(record->horizontal_sigma_m(), 3.0);
  EXPECT_EQ(record->vertical_sigma_m(), 4.0);
  EXPECT_EQ(record->fix_type(), v1::PliRecord::FIX_TYPE_OTHER);
  EXPECT_FALSE(record->has_attitude());
}

TEST(LatticeAdapter, TakesTheSemiMajorAxisOfACorrelatedCovariance) {
  Event event = update();
  // Eigenvalues 3 and 1.
  event.entity.position_covariance_m2 = Covariance{.xx = 2.0, .xy = 1.0, .xz = 0.0, .yy = 2.0, .yz = 0.0, .zz = 0.0};
  const std::optional<v1::PliRecord> record = adapter().record(event, kNoon);
  ASSERT_TRUE(record.has_value());
  EXPECT_NEAR(record->horizontal_sigma_m(), std::sqrt(3.0), 1e-12);
  EXPECT_EQ(record->vertical_sigma_m(), 0.0);
}

TEST(LatticeAdapter, LeavesAnInvalidCovarianceUnset) {
  for (const Covariance& covariance : {Covariance{.xx = -1.0, .xy = 0.0, .xz = 0.0, .yy = 1.0, .yz = 0.0, .zz = -1.0},
                                       Covariance{.xx = 1.0, .xy = 0.0, .xz = 0.0, .yy = -1.0, .yz = 0.0, .zz = -4.0},
                                       Covariance{.xx = 1e308, .xy = 0.0, .xz = 0.0, .yy = 1e308, .yz = 0.0, .zz = -9.0}}) {
    Event event = update();
    event.entity.position_covariance_m2 = covariance;
    const std::optional<v1::PliRecord> record = adapter().record(event, kNoon);
    ASSERT_TRUE(record.has_value());
    EXPECT_FALSE(record->has_horizontal_sigma_m());
    EXPECT_FALSE(record->has_vertical_sigma_m());
  }
}

TEST(LatticeAdapter, RotatesTheVelocityIntoTheRangeFrame) {
  // East at 0 N, 0 E is ECEF Y, which is up at 0 N, 90 E.
  Event event = update();
  event.entity.position = Position{.latitude_deg = 0.0, .longitude_deg = 0.0, .hae_m = 0.0};
  event.entity.velocity_enu_mps = Enu{.east = 2.0, .north = 0.0, .up = 0.0};
  const Adapter far = Adapter::make({}, frames::EnuFrame(point(0.0, 90.0, 0.0))).value();
  const std::optional<v1::PliRecord> record = far.record(event, kNoon);
  ASSERT_TRUE(record.has_value());
  EXPECT_NEAR(record->velocity_enu_mps().east(), 0.0, 1e-12);
  EXPECT_NEAR(record->velocity_enu_mps().north(), 0.0, 1e-12);
  EXPECT_NEAR(record->velocity_enu_mps().up(), 2.0, 1e-12);
}

TEST(LatticeAdapter, LeavesAnUnknownHeightAndItsSigmaUnset) {
  Event event = update();
  event.entity.position->hae_m = std::nullopt;
  event.entity.position_covariance_m2 = Covariance{.xx = 1.0, .xy = 0.0, .xz = 0.0, .yy = 1.0, .yz = 0.0, .zz = 4.0};
  const std::optional<v1::PliRecord> record = adapter().record(event, kNoon);
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->position().height_ellipsoid_m(), 0.0);
  EXPECT_EQ(record->horizontal_sigma_m(), 1.0);
  EXPECT_FALSE(record->has_vertical_sigma_m());
  EXPECT_FALSE(record->has_velocity_enu_mps());
  EXPECT_EQ(record->role(), v1::ENTITY_ROLE_OTHER);
}

TEST(LatticeAdapter, FlagsAReceiptTimeWhenTheSourceTimeIsMissingOrSkewed) {
  const Adapter lattice = adapter();
  Event untimed = update();
  untimed.entity.source_update_time = std::nullopt;
  for (const auto& [event, received] : {std::pair{update(), kNoon + seconds(31)},
                                        std::pair{update(), kNoon - seconds(31)}, std::pair{untimed, kNoon}}) {
    const std::optional<v1::PliRecord> record = lattice.record(event, received);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->time_basis(), v1::PLI_TIME_BASIS_RECEIPT);
    EXPECT_EQ(record->valid_utc_ns(), to_utc_ns(received));
  }
  EXPECT_EQ(lattice.record(update(), kNoon + seconds(30))->time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
}

TEST(LatticeAdapter, RecordsOnlyCurrentPositionsOfLiveEntities) {
  for (const EventType type : {EventType::kPreexisting, EventType::kCreated, EventType::kUpdate}) {
    Event event = update();
    event.type = type;
    EXPECT_TRUE(adapter().record(event, kNoon).has_value());
  }
  for (const EventType type : {EventType::kDeleted, EventType::kOther}) {
    Event event = update();
    event.type = type;
    EXPECT_FALSE(adapter().record(event, kNoon).has_value());
  }
  Event expired = update();
  expired.entity.live = false;
  Event unplaced = update();
  unplaced.entity.position = std::nullopt;
  Event outside = update();
  outside.entity.position->latitude_deg = 100.0;
  for (const Event& event : {expired, unplaced, outside}) {
    EXPECT_FALSE(adapter().record(event, kNoon).has_value());
  }
}

}  // namespace
}  // namespace ics::lattice
