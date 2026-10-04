#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ics::capture {

// The UDP datagrams in captured Ethernet frames, as a TAP port passes them to
// pcap capture: Ethernet II, with at most one 802.1Q tag, carrying IPv4 and
// UDP. The PLI adapters read their feeds from these: MAVLink (ICS-021) and
// Cursor on Target (ICS-022).

struct Endpoint {
  std::array<std::uint8_t, 4> address{};
  std::uint16_t port = 0;
};

// One UDP datagram. The payload refers to the frame's bytes.
struct Datagram {
  Endpoint source;
  Endpoint destination;
  std::span<const std::byte> payload;
};

// The UDP datagram in an Ethernet frame. Nothing for any other frame, and for
// one cut short by the capture's snap length, an IP fragment, or a header
// that contradicts itself.
[[nodiscard]] std::optional<Datagram> udp_datagram(std::span<const std::byte> frame) noexcept;

}  // namespace ics::capture
