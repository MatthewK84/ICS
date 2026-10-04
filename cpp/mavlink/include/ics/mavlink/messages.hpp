#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

#include "ics/common/error.hpp"
#include "ics/mavlink/frame.hpp"

namespace ics::mavlink {

// The MAVLink messages ICS reads (ICS-021), field for field as common.xml
// defines them, extensions included. Fields keep MAVLink's names and units.

inline constexpr std::uint32_t kHeartbeatId = 0;
inline constexpr std::uint32_t kSystemTimeId = 2;
inline constexpr std::uint32_t kGpsRawIntId = 24;
inline constexpr std::uint32_t kAttitudeQuaternionId = 31;
inline constexpr std::uint32_t kGlobalPositionIntId = 33;
inline constexpr std::uint32_t kCommandLongId = 76;
inline constexpr std::uint32_t kCommandAckId = 77;
inline constexpr std::uint32_t kStatusTextId = 253;

struct Heartbeat {
  std::uint8_t type = 0;        // MAV_TYPE
  std::uint8_t autopilot = 0;   // MAV_AUTOPILOT
  std::uint8_t base_mode = 0;   // MAV_MODE_FLAG bits
  std::uint32_t custom_mode = 0;
  std::uint8_t system_status = 0;
  std::uint8_t mavlink_version = 0;
};

struct SystemTime {
  std::uint64_t time_unix_usec = 0;  // UTC; 0 when the vehicle has no time
  std::uint32_t time_boot_ms = 0;
};

struct GpsRawInt {
  std::uint64_t time_usec = 0;
  std::uint8_t fix_type = 0;  // GPS_FIX_TYPE
  std::int32_t lat = 0;       // degrees * 1e7
  std::int32_t lon = 0;       // degrees * 1e7
  std::int32_t alt = 0;       // mm above MSL
  std::uint16_t eph = 0;
  std::uint16_t epv = 0;
  std::uint16_t vel = 0;
  std::uint16_t cog = 0;
  std::uint8_t satellites_visible = 0;
  std::int32_t alt_ellipsoid = 0;  // mm above the WGS84 ellipsoid
  std::uint32_t h_acc = 0;         // mm, one sigma; 0 when not reported
  std::uint32_t v_acc = 0;         // mm, one sigma; 0 when not reported
};

struct AttitudeQuaternion {
  std::uint32_t time_boot_ms = 0;
  // The rotation from the body frame (forward, right, down) to local NED:
  // q1 is the scalar part, q2 to q4 the vector part.
  float q1 = 0.0F;
  float q2 = 0.0F;
  float q3 = 0.0F;
  float q4 = 0.0F;
  float rollspeed = 0.0F;
  float pitchspeed = 0.0F;
  float yawspeed = 0.0F;
};

struct GlobalPositionInt {
  std::uint32_t time_boot_ms = 0;
  std::int32_t lat = 0;           // degrees * 1e7
  std::int32_t lon = 0;           // degrees * 1e7
  std::int32_t alt = 0;           // mm above MSL
  std::int32_t relative_alt = 0;  // mm above home
  std::int16_t vx = 0;            // cm/s north
  std::int16_t vy = 0;            // cm/s east
  std::int16_t vz = 0;            // cm/s down
  std::uint16_t hdg = 0;          // centidegrees; UINT16_MAX when unknown
};

struct CommandLong {
  std::uint8_t target_system = 0;
  std::uint8_t target_component = 0;
  std::uint16_t command = 0;  // MAV_CMD
  std::uint8_t confirmation = 0;
  std::array<float, 7> params{};
};

struct CommandAck {
  std::uint16_t command = 0;  // MAV_CMD
  std::uint8_t result = 0;    // MAV_RESULT
  std::uint8_t progress = 0;
  std::int32_t result_param2 = 0;
  std::uint8_t target_system = 0;
  std::uint8_t target_component = 0;
};

inline constexpr std::size_t kStatusTextSize = 50;

struct StatusText {
  std::uint8_t severity = 0;  // MAV_SEVERITY
  std::array<char, kStatusTextSize> text{};
  std::uint16_t id = 0;
  std::uint8_t chunk_seq = 0;

  // The text up to its first NUL, or all 50 characters when it has none.
  [[nodiscard]] std::string_view view() const noexcept;
};

using Message =
    std::variant<Heartbeat, SystemTime, GpsRawInt, AttitudeQuaternion, GlobalPositionInt, CommandLong, CommandAck,
                 StatusText>;

// The message in a frame. Fails with Error::kInvalidArgument for a message
// ICS does not read, which FrameReader never returns.
[[nodiscard]] Result<Message> decode(const Frame& frame) noexcept;

}  // namespace ics::mavlink
