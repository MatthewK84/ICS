#include "ics/mavlink/adapter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mavlink/frame.hpp"
#include "ics/mavlink/messages.hpp"
#include "mavlink_support.hpp"

namespace ics::mavlink {
namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;
using testing::frame_of;

constexpr std::uint8_t kPx4 = 12;
constexpr std::uint8_t kArduPilot = 3;
constexpr std::uint8_t kQuadrotor = 2;
constexpr std::uint32_t kPosctl = 0x00030000;
constexpr std::uint32_t kAutoMission = 0x04040000;
constexpr std::uint8_t kGroundSystem = 255;
constexpr std::uint8_t kGroundComponent = 190;
const UtcTime kT0 = utc_from_ns(1'790'000'000'500'000'000);

const frames::Egm96& geoid() {
  static const Result<frames::Egm96> grid = frames::Egm96::load(ICS_EGM96_PATH);
  return *grid;
}

frames::Geodetic point(const double latitude, const double longitude, const double height) {
  return frames::Geodetic::make(Degrees(latitude), Degrees(longitude), Meters(height)).value();
}

const GlobalPositionInt kPosition{.time_boot_ms = 654421,
                                  .lat = 400001234,
                                  .lon = -999998765,
                                  .alt = 735250,
                                  .relative_alt = 35250,
                                  .vx = 500,
                                  .vy = -120,
                                  .vz = -30,
                                  .hdg = 9000};

class AdapterTest : public ::testing::Test {
 protected:
  // The range origin at the vehicle in kPosition, so its ENU axes are the
  // vehicle's own.
  AdapterTest()
      : adapter_({.roles = {{.system = 1, .role = v1::ENTITY_ROLE_TARGET}}}, geoid(),
                 frames::EnuFrame(geoid().from_msl(Degrees(40.0001234), Degrees(-99.9998765), Meters(735.25)).value())) {
  }

  void receive(const Frame& frame, const UtcTime at = kT0) { adapter_.receive(frame, at, out_); }

  void heartbeat(const std::uint8_t base_mode, const std::uint32_t custom_mode, const UtcTime at = kT0,
                 const std::uint8_t autopilot = kPx4, const std::uint8_t component = 1) {
    const Heartbeat message{.type = kQuadrotor, .autopilot = autopilot, .base_mode = base_mode, .custom_mode = custom_mode};
    receive(frame_of(kHeartbeatId, testing::heartbeat_payload(message), 1, component), at);
  }

  std::vector<v1::PliEvent::Kind> kinds() const {
    std::vector<v1::PliEvent::Kind> out;
    std::ranges::transform(out_.events, std::back_inserter(out), [](const v1::PliEvent& event) { return event.kind(); });
    return out;
  }

  Adapter adapter_;
  Output out_;
};

TEST_F(AdapterTest, MakesARecordFromAPositionWithTheVehiclesTimeFixAndAttitude) {
  receive(frame_of(kSystemTimeId, testing::system_time_payload({.time_unix_usec = 1790000000123456, .time_boot_ms = 654321})));
  receive(frame_of(kGpsRawIntId, testing::gps_raw_int_payload({.fix_type = 3, .h_acc = 1500, .v_acc = 2500})));
  receive(frame_of(kAttitudeQuaternionId,
                   testing::attitude_payload({.q1 = 0.5F, .q2 = 0.5F, .q3 = -0.5F, .q4 = 0.5F})));
  receive(frame_of(kGlobalPositionIntId, testing::position_payload(kPosition)), kT0 + milliseconds(20));
  ASSERT_EQ(out_.positions.size(), 1U);
  const Position& position = out_.positions[0];
  const v1::PliRecord& record = position.record;
  EXPECT_EQ(position.time_boot_ms, 654421U);
  EXPECT_EQ(record.entity_id(), "1");
  EXPECT_EQ(record.role(), v1::ENTITY_ROLE_TARGET);
  EXPECT_EQ(record.source(), v1::PLI_SOURCE_MAVLINK);
  // 1790000000.123456 s at 654321 ms, so 100 ms later at 654421 ms.
  EXPECT_EQ(record.valid_utc_ns(), 1'790'000'000'223'456'000);
  EXPECT_EQ(record.time_basis(), v1::PLI_TIME_BASIS_VEHICLE_GNSS);
  EXPECT_EQ(record.received_utc_ns(), to_utc_ns(kT0 + milliseconds(20)));
  EXPECT_DOUBLE_EQ(record.position().latitude_deg(), 40.0001234);
  EXPECT_DOUBLE_EQ(record.position().longitude_deg(), -99.9998765);
  const frames::Geodetic at = point(40.0001234, -99.9998765, record.position().height_ellipsoid_m());
  EXPECT_NEAR(geoid().msl_height(at).value(), 735.25, 1e-9);
  EXPECT_NEAR(record.velocity_enu_mps().east(), -1.2, 1e-9);
  EXPECT_NEAR(record.velocity_enu_mps().north(), 5.0, 1e-9);
  EXPECT_NEAR(record.velocity_enu_mps().up(), 0.3, 1e-9);
  EXPECT_EQ(record.fix_type(), v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL);
  EXPECT_DOUBLE_EQ(record.horizontal_sigma_m(), 1.5);
  EXPECT_DOUBLE_EQ(record.vertical_sigma_m(), 2.5);
  ASSERT_TRUE(record.has_attitude());
  EXPECT_EQ(record.attitude().w(), 0.5);
  EXPECT_EQ(record.attitude().x(), 0.5);
  EXPECT_EQ(record.attitude().y(), -0.5);
  EXPECT_EQ(record.attitude().z(), 0.5);
  EXPECT_TRUE(out_.events.empty());
}

TEST_F(AdapterTest, TakesTheReceiptTimeUntilTheVehicleSendsUtc) {
  receive(frame_of(kGlobalPositionIntId, testing::position_payload(kPosition), 9));
  // No UTC yet, a time past 2100, and one from another component.
  receive(frame_of(kSystemTimeId, testing::system_time_payload({.time_unix_usec = 0, .time_boot_ms = 1}), 9));
  receive(frame_of(kSystemTimeId, testing::system_time_payload({.time_unix_usec = 4102444800000000, .time_boot_ms = 1}), 9));
  receive(frame_of(kSystemTimeId, testing::system_time_payload({.time_unix_usec = 1790000000000000, .time_boot_ms = 1}), 9,
                   kGroundComponent));
  receive(frame_of(kGlobalPositionIntId, testing::position_payload(kPosition), 9), kT0 + seconds(1));
  ASSERT_EQ(out_.positions.size(), 2U);
  for (const Position& position : out_.positions) {
    EXPECT_EQ(position.record.time_basis(), v1::PLI_TIME_BASIS_RECEIPT);
    EXPECT_EQ(position.record.valid_utc_ns(), position.record.received_utc_ns());
    EXPECT_EQ(position.record.entity_id(), "9");
    EXPECT_EQ(position.record.role(), v1::ENTITY_ROLE_OTHER);
    EXPECT_EQ(position.record.fix_type(), v1::PliRecord::FIX_TYPE_UNSPECIFIED);
    EXPECT_FALSE(position.record.has_horizontal_sigma_m());
    EXPECT_FALSE(position.record.has_attitude());
  }
  EXPECT_EQ(out_.positions[1].record.valid_utc_ns(), to_utc_ns(kT0 + seconds(1)));
}

TEST_F(AdapterTest, LeavesOutAFixAndAttitudeOlderThanMaxAge) {
  receive(frame_of(kGpsRawIntId, testing::gps_raw_int_payload({.fix_type = 6})), kT0);
  receive(frame_of(kAttitudeQuaternionId, testing::attitude_payload({.q1 = 1.0F})), kT0);
  receive(frame_of(kGlobalPositionIntId, testing::position_payload(kPosition)), kT0 + seconds(1));
  receive(frame_of(kGlobalPositionIntId, testing::position_payload(kPosition)), kT0 + milliseconds(1001));
  ASSERT_EQ(out_.positions.size(), 2U);
  const v1::PliRecord& fresh = out_.positions[0].record;
  EXPECT_EQ(fresh.fix_type(), v1::PliRecord::FIX_TYPE_RTK_FIXED);
  EXPECT_FALSE(fresh.has_horizontal_sigma_m());
  EXPECT_FALSE(fresh.has_vertical_sigma_m());
  EXPECT_TRUE(fresh.has_attitude());
  const v1::PliRecord& stale = out_.positions[1].record;
  EXPECT_EQ(stale.fix_type(), v1::PliRecord::FIX_TYPE_UNSPECIFIED);
  EXPECT_FALSE(stale.has_attitude());
}

TEST_F(AdapterTest, MapsEachGpsFixType) {
  const std::vector<v1::PliRecord::FixType> expected{
      v1::PliRecord::FIX_TYPE_NONE,   v1::PliRecord::FIX_TYPE_NONE,      v1::PliRecord::FIX_TYPE_TWO_DIMENSIONAL,
      v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL, v1::PliRecord::FIX_TYPE_DGNSS, v1::PliRecord::FIX_TYPE_RTK_FLOAT,
      v1::PliRecord::FIX_TYPE_RTK_FIXED, v1::PliRecord::FIX_TYPE_OTHER, v1::PliRecord::FIX_TYPE_THREE_DIMENSIONAL,
      v1::PliRecord::FIX_TYPE_OTHER};
  for (std::uint8_t fix = 0; fix < expected.size(); ++fix) {
    receive(frame_of(kGpsRawIntId, testing::gps_raw_int_payload({.fix_type = fix})));
    receive(frame_of(kGlobalPositionIntId, testing::position_payload(kPosition)));
    EXPECT_EQ(out_.positions.back().record.fix_type(), expected.at(fix)) << int{fix};
  }
}

TEST_F(AdapterTest, IgnoresStateFromOtherComponents) {
  receive(frame_of(kGpsRawIntId, testing::gps_raw_int_payload({.fix_type = 3}), 1, kGroundComponent));
  receive(frame_of(kAttitudeQuaternionId, testing::attitude_payload({.q1 = 1.0F}), 1, kGroundComponent));
  receive(frame_of(kGlobalPositionIntId, testing::position_payload(kPosition), 1, kGroundComponent));
  EXPECT_TRUE(out_.positions.empty());
  receive(frame_of(kGlobalPositionIntId, testing::position_payload(kPosition)));
  ASSERT_EQ(out_.positions.size(), 1U);
  EXPECT_EQ(out_.positions[0].record.fix_type(), v1::PliRecord::FIX_TYPE_UNSPECIFIED);
  EXPECT_FALSE(out_.positions[0].record.has_attitude());
}

TEST_F(AdapterTest, CountsPositionsOutOfRange) {
  GlobalPositionInt bad = kPosition;
  bad.lat = 1000000000;  // 100 degrees
  receive(frame_of(kGlobalPositionIntId, testing::position_payload(bad)));
  EXPECT_TRUE(out_.positions.empty());
  EXPECT_EQ(adapter_.counts().bad_positions, 1U);
}

TEST(AdapterVelocity, RotatesIntoTheRangeFrame) {
  // A vehicle at 0 N, 0 E moving east is moving along ECEF Y, which is up at
  // 0 N, 90 E.
  Adapter adapter({}, geoid(), frames::EnuFrame(point(0.0, 90.0, 0.0)));
  Output out;
  GlobalPositionInt east = kPosition;
  east.lat = 0;
  east.lon = 0;
  east.vx = 0;
  east.vy = 100;
  east.vz = 0;
  adapter.receive(frame_of(kGlobalPositionIntId, testing::position_payload(east)), kT0, out);
  ASSERT_EQ(out.positions.size(), 1U);
  const v1::EnuVector& velocity = out.positions[0].record.velocity_enu_mps();
  EXPECT_NEAR(velocity.east(), 0.0, 1e-12);
  EXPECT_NEAR(velocity.north(), 0.0, 1e-12);
  EXPECT_NEAR(velocity.up(), 1.0, 1e-12);
}

TEST_F(AdapterTest, ReportsArmingAndModeChanges) {
  heartbeat(0x01, kPosctl);
  heartbeat(0x01, kPosctl);
  heartbeat(0x81, kPosctl);
  heartbeat(0x81, kAutoMission);
  heartbeat(0x01, kAutoMission);
  EXPECT_EQ(kinds(), (std::vector<v1::PliEvent::Kind>{v1::PliEvent::KIND_MODE_CHANGED, v1::PliEvent::KIND_ARMED,
                                                     v1::PliEvent::KIND_MODE_CHANGED, v1::PliEvent::KIND_DISARMED}));
  EXPECT_EQ(out_.events[0].detail(), "POSCTL");
  EXPECT_EQ(out_.events[2].detail(), "AUTO.MISSION");
  for (const v1::PliEvent& event : out_.events) {
    EXPECT_EQ(event.entity_id(), "1");
    EXPECT_EQ(event.source(), v1::PLI_SOURCE_MAVLINK);
    EXPECT_EQ(event.time_utc_ns(), to_utc_ns(kT0));
    EXPECT_EQ(event.time_basis(), v1::PLI_TIME_BASIS_RECEIPT);
  }
}

TEST_F(AdapterTest, ReportsAVehicleArmedAtItsFirstHeartbeat) {
  heartbeat(0x81, 5, kT0, kArduPilot);
  EXPECT_EQ(kinds(), (std::vector<v1::PliEvent::Kind>{v1::PliEvent::KIND_ARMED, v1::PliEvent::KIND_MODE_CHANGED}));
  EXPECT_EQ(out_.events[1].detail(), "LOITER");
}

TEST_F(AdapterTest, IgnoresGroundStationAndOtherComponentHeartbeats) {
  heartbeat(0x81, 0, kT0, 8);
  heartbeat(0x81, 0, kT0, kPx4, kGroundComponent);
  adapter_.tick(kT0 + seconds(10), out_);
  EXPECT_TRUE(out_.events.empty());
}

TEST_F(AdapterTest, ReportsALinkLostAndRestored) {
  heartbeat(0x01, kPosctl);
  out_.events.clear();
  adapter_.tick(kT0 + seconds(3), out_);
  EXPECT_TRUE(out_.events.empty());
  adapter_.tick(kT0 + milliseconds(3500), out_);
  adapter_.tick(kT0 + seconds(4), out_);
  ASSERT_EQ(kinds(), (std::vector<v1::PliEvent::Kind>{v1::PliEvent::KIND_LINK_LOST}));
  EXPECT_EQ(out_.events[0].time_utc_ns(), to_utc_ns(kT0 + seconds(3)));
  heartbeat(0x01, kPosctl, kT0 + seconds(5));
  EXPECT_EQ(kinds(), (std::vector<v1::PliEvent::Kind>{v1::PliEvent::KIND_LINK_LOST, v1::PliEvent::KIND_LINK_RESTORED}));
  EXPECT_EQ(out_.events[1].time_utc_ns(), to_utc_ns(kT0 + seconds(5)));
  adapter_.tick(kT0 + seconds(7), out_);
  EXPECT_EQ(out_.events.size(), 2U);
}

TEST_F(AdapterTest, ReportsCommandsAcknowledgementsAndText) {
  receive(frame_of(kCommandLongId,
                   testing::command_long_payload(
                       {.target_system = 2, .target_component = 1, .command = 400, .params = {1.0F, 21196.0F}}),
                   kGroundSystem, kGroundComponent));
  receive(frame_of(kCommandAckId, testing::command_ack_payload({.command = 400, .result = 4}), 2));
  receive(frame_of(kStatusTextId, testing::status_text_payload(6, "Arm: \x01\xffok\x7f"), 2, 0));
  ASSERT_EQ(kinds(), (std::vector<v1::PliEvent::Kind>{v1::PliEvent::KIND_COMMAND, v1::PliEvent::KIND_COMMAND_ACK,
                                                     v1::PliEvent::KIND_STATUS_TEXT}));
  EXPECT_EQ(out_.events[0].entity_id(), "2");
  EXPECT_EQ(out_.events[0].command(), 400U);
  EXPECT_EQ(out_.events[0].detail(), "params 1 21196 0 0 0 0 0");
  EXPECT_EQ(out_.events[1].entity_id(), "2");
  EXPECT_EQ(out_.events[1].command(), 400U);
  EXPECT_EQ(out_.events[1].command_result(), 4U);
  EXPECT_EQ(out_.events[2].entity_id(), "2");
  EXPECT_EQ(out_.events[2].detail(), "Arm: ??ok?");
}

TEST_F(AdapterTest, IgnoresAFrameItCannotDecode) {
  Frame frame;
  frame.message_id = 42;
  receive(frame);
  EXPECT_TRUE(out_.positions.empty());
  EXPECT_TRUE(out_.events.empty());
}

}  // namespace
}  // namespace ics::mavlink
