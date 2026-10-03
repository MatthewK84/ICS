#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <iterator>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ics/mavlink/frame.hpp"
#include "ics/mavlink/messages.hpp"

namespace ics::mavlink::testing {

using Bytes = std::vector<std::byte>;

// Frames the SITL rig's own encoder (python/ics_sitl/mavlink.py) wrote, with
// sequence 7, as cross-checks on the checksum, the CRC_EXTRA table, the
// field offsets and MAVLink 2's trimming of trailing zeros:
// HEARTBEAT from PX4 (system 1) in AUTO.MISSION, armed;
inline constexpr std::string_view kHeartbeat = "fd09000007010100000000000404020c89040350a2";
// SYSTEM_TIME, 1790000000.123456 s at 654321 ms after boot;
inline constexpr std::string_view kSystemTime = "fd0b000007010102000040c227dafe5b0600f1fb090b18";
// GPS_RAW_INT from ArduCopter (system 2), a 3D fix at 40 N, 100 W, 735 m;
inline constexpr std::string_view kGpsRawInt =
    "fd21000007020118000040c227dafe5b06000084d717003665c418370b007800b400f4012823030c99de0a0ff0";
// ATTITUDE_QUATERNION, level and facing north, with body rates 0.5, -0.25
// and 0.125 rad/s;
inline constexpr std::string_view kAttitudeQuaternion =
    "fd2000000701011f0000dcfb09000000803f0000000000000000000000000000003f000080be0000003e8376";
// GLOBAL_POSITION_INT at 40.0001234 N, 99.9998765 W, 735.25 m MSL, 5 m/s
// north, 1.2 m/s west, 0.3 m/s up;
inline constexpr std::string_view kGlobalPositionInt =
    "fd1c0000070101210000f1fb0900d288d717d33a65c412380b00b2890000f40188ffe2ff282333f7";
// COMMAND_LONG from the rig (system 255, component 190): arm system 2;
inline constexpr std::string_view kCommandLong =
    "fd20000007ffbe4c00000000803f00000000000000000000000000000000000000000000000090010201615d";
// COMMAND_ACK from system 2: the arm command temporarily rejected;
inline constexpr std::string_view kCommandAck = "fd0a00000702014d00009001040000000000ffbe8386";
// STATUSTEXT from system 2, severity INFO.
inline constexpr std::string_view kStatusText =
    "fd1f0000070201fd00000650726541726d3a204e65656420506f736974696f6e20457374696d61746535cc";

inline Bytes from_hex(const std::string_view hex) {
  Bytes out;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    out.push_back(static_cast<std::byte>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
  }
  return out;
}

inline void put_le(Bytes& out, const std::uint64_t value, const std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
  }
}

inline void put_be16(Bytes& out, const unsigned value) {
  out.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::byte>(value & 0xFFU));
}

inline Bytes concat(const std::vector<Bytes>& parts) {
  Bytes out;
  for (const Bytes& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

struct Sender {
  std::uint8_t system = 1;
  std::uint8_t component = 1;
  std::uint8_t sequence = 0;
};

// The checksum of a frame's bytes after the magic, then the CRC_EXTRA.
inline std::uint16_t checksum(const Bytes& header_and_payload, const std::uint8_t extra) {
  const std::array<std::byte, 1> extra_byte{static_cast<std::byte>(extra)};
  return x25(extra_byte, x25(std::span<const std::byte>(header_and_payload).subspan(1)));
}

// A MAVLink 2 frame of payload, as given (not trimmed), with the
// incompatibility flags given and a signature of 13 bytes when signed.
inline Bytes frame_v2(const std::uint32_t message_id, const std::uint8_t extra, const Bytes& payload,
                      const Sender sender = {}, const std::uint8_t incompatible = 0) {
  Bytes out{std::byte{0xFD}, static_cast<std::byte>(payload.size()), static_cast<std::byte>(incompatible),
            std::byte{0}};
  out.push_back(static_cast<std::byte>(sender.sequence));
  out.push_back(static_cast<std::byte>(sender.system));
  out.push_back(static_cast<std::byte>(sender.component));
  put_le(out, message_id, 3);
  out.insert(out.end(), payload.begin(), payload.end());
  put_le(out, checksum(out, extra), 2);
  if ((incompatible & 0x01U) != 0U) {
    out.insert(out.end(), 13, std::byte{0x5A});
  }
  return out;
}

// A MAVLink 1 frame of payload.
inline Bytes frame_v1(const std::uint8_t message_id, const std::uint8_t extra, const Bytes& payload,
                      const Sender sender = {}) {
  Bytes out{std::byte{0xFE}, static_cast<std::byte>(payload.size())};
  out.push_back(static_cast<std::byte>(sender.sequence));
  out.push_back(static_cast<std::byte>(sender.system));
  out.push_back(static_cast<std::byte>(sender.component));
  out.push_back(static_cast<std::byte>(message_id));
  out.insert(out.end(), payload.begin(), payload.end());
  put_le(out, checksum(out, extra), 2);
  return out;
}

// The payload of a frame built by frame_v2, untrimmed.
inline Bytes payload_of(const Bytes& frame) {
  const std::size_t length = std::to_integer<std::size_t>(frame[1]);
  return Bytes(frame.begin() + 10, frame.begin() + 10 + static_cast<std::ptrdiff_t>(length));
}

struct UdpAddressing {
  std::array<std::uint8_t, 4> source{172, 30, 18, 10};
  std::array<std::uint8_t, 4> destination{172, 30, 18, 1};
  std::uint16_t source_port = 18570;
  std::uint16_t destination_port = 14551;
};

// An IPv4 packet carrying payload in UDP.
inline Bytes ipv4_udp(const Bytes& payload, const UdpAddressing& to = {}) {
  Bytes out;
  put_be16(out, 0x4500);
  put_be16(out, static_cast<unsigned>(20 + 8 + payload.size()));
  put_be16(out, 0x1234);
  put_be16(out, 0x4000);  // don't fragment
  out.push_back(std::byte{64});
  out.push_back(std::byte{17});
  put_be16(out, 0);
  const auto as_byte = [](const std::uint8_t octet) { return static_cast<std::byte>(octet); };
  std::ranges::transform(to.source, std::back_inserter(out), as_byte);
  std::ranges::transform(to.destination, std::back_inserter(out), as_byte);
  put_be16(out, to.source_port);
  put_be16(out, to.destination_port);
  put_be16(out, static_cast<unsigned>(8 + payload.size()));
  put_be16(out, 0);
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

// An Ethernet II frame of an IPv4 packet, with an 802.1Q tag when tagged.
inline Bytes ethernet(const Bytes& packet, const bool tagged = false, const unsigned ether_type = 0x0800) {
  Bytes out(12, std::byte{0x02});
  if (tagged) {
    put_be16(out, 0x8100);
    put_be16(out, 0x0064);
  }
  put_be16(out, ether_type);
  out.insert(out.end(), packet.begin(), packet.end());
  return out;
}

// Writes value, size bytes little-endian, at offset at of payload.
inline void put_at(Bytes& payload, const std::size_t at, const std::uint64_t value, const std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    payload.at(at + i) = static_cast<std::byte>((value >> (8 * i)) & 0xFFU);
  }
}

inline std::uint32_t bits(const float value) { return std::bit_cast<std::uint32_t>(value); }

// A frame of message_id with payload, from system and component.
inline Frame frame_of(const std::uint32_t message_id, const Bytes& payload, const std::uint8_t system = 1,
                      const std::uint8_t component = 1) {
  Frame frame;
  frame.system = system;
  frame.component = component;
  frame.message_id = message_id;
  std::copy(payload.begin(), payload.end(), frame.payload.begin());
  return frame;
}

inline Bytes heartbeat_payload(const Heartbeat& message) {
  Bytes out(9);
  put_at(out, 0, message.custom_mode, 4);
  put_at(out, 4, message.type, 1);
  put_at(out, 5, message.autopilot, 1);
  put_at(out, 6, message.base_mode, 1);
  put_at(out, 7, message.system_status, 1);
  put_at(out, 8, message.mavlink_version, 1);
  return out;
}

inline Bytes system_time_payload(const SystemTime& message) {
  Bytes out(12);
  put_at(out, 0, message.time_unix_usec, 8);
  put_at(out, 8, message.time_boot_ms, 4);
  return out;
}

inline Bytes gps_raw_int_payload(const GpsRawInt& message) {
  Bytes out(52);
  put_at(out, 0, message.time_usec, 8);
  put_at(out, 8, static_cast<std::uint32_t>(message.lat), 4);
  put_at(out, 12, static_cast<std::uint32_t>(message.lon), 4);
  put_at(out, 16, static_cast<std::uint32_t>(message.alt), 4);
  put_at(out, 28, message.fix_type, 1);
  put_at(out, 29, message.satellites_visible, 1);
  put_at(out, 34, message.h_acc, 4);
  put_at(out, 38, message.v_acc, 4);
  return out;
}

inline Bytes attitude_payload(const AttitudeQuaternion& message) {
  Bytes out(32);
  put_at(out, 0, message.time_boot_ms, 4);
  put_at(out, 4, bits(message.q1), 4);
  put_at(out, 8, bits(message.q2), 4);
  put_at(out, 12, bits(message.q3), 4);
  put_at(out, 16, bits(message.q4), 4);
  return out;
}

inline Bytes position_payload(const GlobalPositionInt& message) {
  Bytes out(28);
  put_at(out, 0, message.time_boot_ms, 4);
  put_at(out, 4, static_cast<std::uint32_t>(message.lat), 4);
  put_at(out, 8, static_cast<std::uint32_t>(message.lon), 4);
  put_at(out, 12, static_cast<std::uint32_t>(message.alt), 4);
  put_at(out, 16, static_cast<std::uint32_t>(message.relative_alt), 4);
  put_at(out, 20, static_cast<std::uint16_t>(message.vx), 2);
  put_at(out, 22, static_cast<std::uint16_t>(message.vy), 2);
  put_at(out, 24, static_cast<std::uint16_t>(message.vz), 2);
  put_at(out, 26, message.hdg, 2);
  return out;
}

inline Bytes command_long_payload(const CommandLong& message) {
  Bytes out(33);
  for (std::size_t i = 0; i < message.params.size(); ++i) {
    put_at(out, 4 * i, bits(message.params.at(i)), 4);
  }
  put_at(out, 28, message.command, 2);
  put_at(out, 30, message.target_system, 1);
  put_at(out, 31, message.target_component, 1);
  put_at(out, 32, message.confirmation, 1);
  return out;
}

inline Bytes command_ack_payload(const CommandAck& message) {
  Bytes out(10);
  put_at(out, 0, message.command, 2);
  put_at(out, 2, message.result, 1);
  put_at(out, 8, message.target_system, 1);
  put_at(out, 9, message.target_component, 1);
  return out;
}

inline Bytes status_text_payload(const std::uint8_t severity, const std::string_view text) {
  Bytes out(54);
  out.at(0) = static_cast<std::byte>(severity);
  for (std::size_t i = 0; i < text.size() && i < 50; ++i) {
    out.at(1 + i) = static_cast<std::byte>(text[i]);
  }
  return out;
}

}  // namespace ics::mavlink::testing
