#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// Builders for the captured frames the PLI adapters read: UDP in IPv4 in
// Ethernet II (ICS-021, ICS-022).
namespace ics::capture::testing {

using Bytes = std::vector<std::byte>;

inline Bytes from_hex(const std::string_view hex) {
  Bytes out;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    out.push_back(static_cast<std::byte>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
  }
  return out;
}

inline void put_be16(Bytes& out, const unsigned value) {
  out.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::byte>(value & 0xFFU));
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

}  // namespace ics::capture::testing
