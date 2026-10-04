#include "ics/mavlink/messages.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <span>
#include <string_view>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"

namespace ics::mavlink {
namespace {

constexpr unsigned kBitsPerByte = 8;
// STATUSTEXT's fields after its severity byte: the text, then the extensions.
constexpr std::size_t kTextAt = 1;
constexpr std::size_t kTextIdAt = kTextAt + kStatusTextSize;
constexpr std::size_t kChunkAt = kTextIdAt + 2;

// The little-endian fields of a payload, at their wire offsets. MAVLink sends
// a message's base fields largest first, then its extensions as declared.
class Fields {
 public:
  explicit Fields(const Frame& frame) noexcept : payload_(frame.payload) {}

  [[nodiscard]] std::uint8_t u8(const std::size_t at) const noexcept {
    return static_cast<std::uint8_t>(read(at, sizeof(std::uint8_t)));
  }
  [[nodiscard]] std::uint16_t u16(const std::size_t at) const noexcept {
    return static_cast<std::uint16_t>(read(at, sizeof(std::uint16_t)));
  }
  [[nodiscard]] std::uint32_t u32(const std::size_t at) const noexcept {
    return static_cast<std::uint32_t>(read(at, sizeof(std::uint32_t)));
  }
  [[nodiscard]] std::uint64_t u64(const std::size_t at) const noexcept { return read(at, sizeof(std::uint64_t)); }
  [[nodiscard]] std::int16_t i16(const std::size_t at) const noexcept { return std::bit_cast<std::int16_t>(u16(at)); }
  [[nodiscard]] std::int32_t i32(const std::size_t at) const noexcept { return std::bit_cast<std::int32_t>(u32(at)); }
  [[nodiscard]] float f32(const std::size_t at) const noexcept { return std::bit_cast<float>(u32(at)); }

  [[nodiscard]] std::span<const std::byte> bytes(const std::size_t at, const std::size_t size) const noexcept {
    static_cast<void>(check(at + size <= payload_.size()));
    return std::span<const std::byte>(payload_).subspan(at, size);
  }

 private:
  [[nodiscard]] std::uint64_t read(const std::size_t at, const std::size_t size) const noexcept {
    const std::span<const std::byte> field = bytes(at, size);
    return std::accumulate(field.rbegin(), field.rend(), std::uint64_t{0},
                           [](const std::uint64_t value, const std::byte byte) {
                             return (value << kBitsPerByte) | std::to_integer<std::uint64_t>(byte);
                           });
  }

  const std::array<std::byte, kMaxPayload>& payload_;
};

[[nodiscard]] Heartbeat heartbeat(const Fields& fields) noexcept {
  return Heartbeat{.type = fields.u8(4),
                   .autopilot = fields.u8(5),
                   .base_mode = fields.u8(6),
                   .custom_mode = fields.u32(0),
                   .system_status = fields.u8(7),
                   .mavlink_version = fields.u8(8)};
}

[[nodiscard]] SystemTime system_time(const Fields& fields) noexcept {
  return SystemTime{.time_unix_usec = fields.u64(0), .time_boot_ms = fields.u32(8)};
}

[[nodiscard]] GpsRawInt gps_raw_int(const Fields& fields) noexcept {
  return GpsRawInt{.time_usec = fields.u64(0),
                   .fix_type = fields.u8(28),
                   .lat = fields.i32(8),
                   .lon = fields.i32(12),
                   .alt = fields.i32(16),
                   .eph = fields.u16(20),
                   .epv = fields.u16(22),
                   .vel = fields.u16(24),
                   .cog = fields.u16(26),
                   .satellites_visible = fields.u8(29),
                   .alt_ellipsoid = fields.i32(30),
                   .h_acc = fields.u32(34),
                   .v_acc = fields.u32(38)};
}

[[nodiscard]] AttitudeQuaternion attitude_quaternion(const Fields& fields) noexcept {
  return AttitudeQuaternion{.time_boot_ms = fields.u32(0),
                            .q1 = fields.f32(4),
                            .q2 = fields.f32(8),
                            .q3 = fields.f32(12),
                            .q4 = fields.f32(16),
                            .rollspeed = fields.f32(20),
                            .pitchspeed = fields.f32(24),
                            .yawspeed = fields.f32(28)};
}

[[nodiscard]] GlobalPositionInt global_position_int(const Fields& fields) noexcept {
  return GlobalPositionInt{.time_boot_ms = fields.u32(0),
                           .lat = fields.i32(4),
                           .lon = fields.i32(8),
                           .alt = fields.i32(12),
                           .relative_alt = fields.i32(16),
                           .vx = fields.i16(20),
                           .vy = fields.i16(22),
                           .vz = fields.i16(24),
                           .hdg = fields.u16(26)};
}

[[nodiscard]] CommandLong command_long(const Fields& fields) noexcept {
  CommandLong command{.target_system = fields.u8(30),
                      .target_component = fields.u8(31),
                      .command = fields.u16(28),
                      .confirmation = fields.u8(32),
                      .params = {}};
  std::size_t at = 0;
  std::ranges::generate(command.params, [&fields, &at] {
    const float param = fields.f32(at);
    at += sizeof(float);
    return param;
  });
  return command;
}

[[nodiscard]] CommandAck command_ack(const Fields& fields) noexcept {
  return CommandAck{.command = fields.u16(0),
                    .result = fields.u8(2),
                    .progress = fields.u8(3),
                    .result_param2 = fields.i32(4),
                    .target_system = fields.u8(8),
                    .target_component = fields.u8(9)};
}

[[nodiscard]] StatusText status_text(const Fields& fields) noexcept {
  StatusText text{.severity = fields.u8(0), .text = {}, .id = fields.u16(kTextIdAt), .chunk_seq = fields.u8(kChunkAt)};
  std::ranges::transform(fields.bytes(kTextAt, kStatusTextSize), text.text.begin(),
                         [](const std::byte byte) { return static_cast<char>(byte); });
  return text;
}

}  // namespace

std::string_view StatusText::view() const noexcept {
  const auto* const end = std::ranges::find(text, '\0');
  return {text.data(), static_cast<std::size_t>(std::distance(text.begin(), end))};
}

Result<Message> decode(const Frame& frame) noexcept {
  const Fields fields(frame);
  switch (frame.message_id) {
    case kHeartbeatId:
      return Message{heartbeat(fields)};
    case kSystemTimeId:
      return Message{system_time(fields)};
    case kGpsRawIntId:
      return Message{gps_raw_int(fields)};
    case kAttitudeQuaternionId:
      return Message{attitude_quaternion(fields)};
    case kGlobalPositionIntId:
      return Message{global_position_int(fields)};
    case kCommandLongId:
      return Message{command_long(fields)};
    case kCommandAckId:
      return Message{command_ack(fields)};
    case kStatusTextId:
      return Message{status_text(fields)};
    default:
      return fail(Error::kInvalidArgument);
  }
}

}  // namespace ics::mavlink
