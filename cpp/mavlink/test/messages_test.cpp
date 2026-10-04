#include "ics/mavlink/messages.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/mavlink/frame.hpp"
#include "mavlink_support.hpp"

namespace ics::mavlink {
namespace {

using testing::Bytes;
using testing::from_hex;

// The message in a frame's bytes, which must hold one frame.
template <typename T>
T only(const Bytes& bytes) {
  FrameReader reader(bytes);
  const std::optional<Frame> frame = reader.next();
  EXPECT_TRUE(frame.has_value());
  const Result<Message> message = decode(frame.value_or(Frame{}));
  EXPECT_TRUE(message.has_value());
  EXPECT_TRUE(std::holds_alternative<T>(message.value_or(Message{})));
  return std::get<T>(message.value_or(Message{T{}}));
}

TEST(Decode, Heartbeat) {
  const auto heartbeat = only<Heartbeat>(from_hex(testing::kHeartbeat));
  EXPECT_EQ(heartbeat.type, 2);
  EXPECT_EQ(heartbeat.autopilot, 12);
  EXPECT_EQ(heartbeat.base_mode, 0x89);
  EXPECT_EQ(heartbeat.custom_mode, 0x04040000U);
  EXPECT_EQ(heartbeat.system_status, 4);
  EXPECT_EQ(heartbeat.mavlink_version, 3);
}

TEST(Decode, SystemTime) {
  const auto time = only<SystemTime>(from_hex(testing::kSystemTime));
  EXPECT_EQ(time.time_unix_usec, 1790000000123456U);
  EXPECT_EQ(time.time_boot_ms, 654321U);
}

TEST(Decode, GpsRawIntWithATrimmedExtension) {
  const auto gps = only<GpsRawInt>(from_hex(testing::kGpsRawInt));
  EXPECT_EQ(gps.time_usec, 1790000000123456U);
  EXPECT_EQ(gps.fix_type, 3);
  EXPECT_EQ(gps.lat, 400000000);
  EXPECT_EQ(gps.lon, -1000000000);
  EXPECT_EQ(gps.alt, 735000);
  EXPECT_EQ(gps.eph, 120);
  EXPECT_EQ(gps.epv, 180);
  EXPECT_EQ(gps.vel, 500);
  EXPECT_EQ(gps.cog, 9000);
  EXPECT_EQ(gps.satellites_visible, 12);
  EXPECT_EQ(gps.alt_ellipsoid, 712345);
  EXPECT_EQ(gps.h_acc, 0U);
  EXPECT_EQ(gps.v_acc, 0U);
}

TEST(Decode, GpsRawIntAccuracies) {
  Bytes payload = testing::payload_of(from_hex(testing::kGpsRawInt));
  payload.resize(30);
  testing::put_le(payload, 712345, 4);
  testing::put_le(payload, 1500, 4);  // h_acc
  testing::put_le(payload, 2500, 4);  // v_acc
  const auto gps = only<GpsRawInt>(testing::frame_v2(kGpsRawIntId, 24, payload));
  EXPECT_EQ(gps.h_acc, 1500U);
  EXPECT_EQ(gps.v_acc, 2500U);
}

TEST(Decode, AttitudeQuaternion) {
  const auto attitude = only<AttitudeQuaternion>(from_hex(testing::kAttitudeQuaternion));
  EXPECT_EQ(attitude.time_boot_ms, 654300U);
  EXPECT_EQ(attitude.q1, 1.0F);
  EXPECT_EQ(attitude.q2, 0.0F);
  EXPECT_EQ(attitude.q3, 0.0F);
  EXPECT_EQ(attitude.q4, 0.0F);
  EXPECT_EQ(attitude.rollspeed, 0.5F);
  EXPECT_EQ(attitude.pitchspeed, -0.25F);
  EXPECT_EQ(attitude.yawspeed, 0.125F);
}

TEST(Decode, GlobalPositionInt) {
  const auto position = only<GlobalPositionInt>(from_hex(testing::kGlobalPositionInt));
  EXPECT_EQ(position.time_boot_ms, 654321U);
  EXPECT_EQ(position.lat, 400001234);
  EXPECT_EQ(position.lon, -999998765);
  EXPECT_EQ(position.alt, 735250);
  EXPECT_EQ(position.relative_alt, 35250);
  EXPECT_EQ(position.vx, 500);
  EXPECT_EQ(position.vy, -120);
  EXPECT_EQ(position.vz, -30);
  EXPECT_EQ(position.hdg, 9000);
}

TEST(Decode, CommandLong) {
  const auto command = only<CommandLong>(from_hex(testing::kCommandLong));
  EXPECT_EQ(command.target_system, 2);
  EXPECT_EQ(command.target_component, 1);
  EXPECT_EQ(command.command, 400);
  EXPECT_EQ(command.confirmation, 0);
  EXPECT_EQ(command.params[0], 1.0F);
  EXPECT_EQ(command.params[6], 0.0F);
}

TEST(Decode, CommandLongParamsInOrder) {
  Bytes payload;
  for (std::uint32_t bits : {0x3F800000U, 0x40000000U, 0x40400000U, 0x40800000U, 0x40A00000U, 0x40C00000U,
                             0x40E00000U}) {
    testing::put_le(payload, bits, 4);
  }
  testing::put_le(payload, 16, 2);  // MAV_CMD_NAV_WAYPOINT
  payload.insert(payload.end(), {std::byte{1}, std::byte{1}, std::byte{2}});
  const auto command = only<CommandLong>(testing::frame_v2(kCommandLongId, 152, payload));
  EXPECT_EQ(command.params, (std::array<float, 7>{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F}));
  EXPECT_EQ(command.command, 16);
  EXPECT_EQ(command.confirmation, 2);
}

TEST(Decode, CommandAck) {
  const auto ack = only<CommandAck>(from_hex(testing::kCommandAck));
  EXPECT_EQ(ack.command, 400);
  EXPECT_EQ(ack.result, 4);
  EXPECT_EQ(ack.progress, 0);
  EXPECT_EQ(ack.result_param2, 0);
  EXPECT_EQ(ack.target_system, 255);
  EXPECT_EQ(ack.target_component, 190);
}

TEST(Decode, StatusText) {
  const auto text = only<StatusText>(from_hex(testing::kStatusText));
  EXPECT_EQ(text.severity, 6);
  EXPECT_EQ(text.view(), "PreArm: Need Position Estimate");
  EXPECT_EQ(text.id, 0);
  EXPECT_EQ(text.chunk_seq, 0);
}

TEST(Decode, StatusTextOfAllFiftyCharactersWithChunkFields) {
  Bytes payload{std::byte{4}};
  payload.insert(payload.end(), kStatusTextSize, std::byte{'x'});
  testing::put_le(payload, 0x1234, 2);
  payload.push_back(std::byte{3});
  const auto text = only<StatusText>(testing::frame_v2(kStatusTextId, 83, payload));
  EXPECT_EQ(text.view(), std::string(kStatusTextSize, 'x'));
  EXPECT_EQ(text.id, 0x1234);
  EXPECT_EQ(text.chunk_seq, 3);
}

TEST(Decode, RefusesAMessageIcsDoesNotRead) {
  Frame frame;
  frame.message_id = 42;
  EXPECT_EQ(decode(frame).error(), Error::kInvalidArgument);
}

}  // namespace
}  // namespace ics::mavlink
