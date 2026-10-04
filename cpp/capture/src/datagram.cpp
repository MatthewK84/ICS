#include "ics/capture/datagram.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "ics/common/check.hpp"

namespace ics::capture {
namespace {

using Bytes = std::span<const std::byte>;

constexpr unsigned kBitsPerByte = 8;
// Ethernet II: destination, source, EtherType. An 802.1Q tag puts four bytes,
// the last two of them the real EtherType, where the EtherType was.
constexpr std::size_t kEtherTypeAt = 12;
constexpr std::size_t kEtherTypeSize = 2;
constexpr std::size_t kTagSize = 4;
constexpr unsigned kEtherTypeIpv4 = 0x0800;
constexpr unsigned kEtherTypeTag = 0x8100;
// IPv4 (RFC 791): version and header length, total length, fragment flags and
// offset, protocol, source and destination addresses.
constexpr std::size_t kIpv4MinHeader = 20;
constexpr unsigned kIpv4 = 4;
constexpr unsigned kNibbleShift = 4;
constexpr unsigned kLowNibble = 0x0FU;
constexpr std::size_t kWordSize = 4;
constexpr std::size_t kTotalLengthAt = 2;
constexpr std::size_t kFragmentAt = 6;
constexpr unsigned kFragmented = 0x3FFFU;  // more fragments, or a non-zero offset
constexpr std::size_t kProtocolAt = 9;
constexpr unsigned kProtocolUdp = 17;
constexpr std::size_t kSourceAt = 12;
constexpr std::size_t kDestinationAt = 16;
// UDP (RFC 768): source port, destination port, length, checksum.
constexpr std::size_t kUdpHeader = 8;
constexpr std::size_t kUdpLengthAt = 4;

[[nodiscard]] unsigned u8(const Bytes data, const std::size_t at) noexcept {
  return std::to_integer<unsigned>(data[at]);
}

[[nodiscard]] unsigned be16(const Bytes data, const std::size_t at) noexcept {
  return (u8(data, at) << kBitsPerByte) | u8(data, at + 1);
}

[[nodiscard]] Endpoint endpoint(const Bytes packet, const std::size_t address_at, const Bytes segment,
                                const std::size_t port_at) noexcept {
  Endpoint out;
  std::ranges::transform(packet.subspan(address_at, out.address.size()), out.address.begin(),
                         [](const std::byte byte) { return std::to_integer<std::uint8_t>(byte); });
  out.port = static_cast<std::uint16_t>(be16(segment, port_at));
  return out;
}

// The IPv4 packet an Ethernet frame carries, or nothing.
[[nodiscard]] std::optional<Bytes> ipv4_packet(const Bytes frame) noexcept {
  std::size_t at = kEtherTypeAt;
  if (frame.size() < at + kEtherTypeSize) {
    return std::nullopt;
  }
  if (be16(frame, at) == kEtherTypeTag) {
    at += kTagSize;
    if (frame.size() < at + kEtherTypeSize) {
      return std::nullopt;
    }
  }
  if (be16(frame, at) != kEtherTypeIpv4) {
    return std::nullopt;
  }
  return frame.subspan(at + kEtherTypeSize);
}

// The packet, trimmed to its total length (an Ethernet frame may be padded),
// if it is a whole, unfragmented IPv4 packet carrying UDP.
[[nodiscard]] std::optional<Bytes> udp_packet(const Bytes packet) noexcept {
  if (packet.size() < kIpv4MinHeader) {
    return std::nullopt;
  }
  const std::size_t header = (u8(packet, 0) & kLowNibble) * kWordSize;
  const std::size_t total = be16(packet, kTotalLengthAt);
  if ((u8(packet, 0) >> kNibbleShift) != kIpv4) {
    return std::nullopt;
  }
  if (header < kIpv4MinHeader || total < header + kUdpHeader) {
    return std::nullopt;
  }
  if (total > packet.size() || (be16(packet, kFragmentAt) & kFragmented) != 0U) {
    return std::nullopt;
  }
  if (u8(packet, kProtocolAt) != kProtocolUdp) {
    return std::nullopt;
  }
  return packet.first(total);
}

}  // namespace

std::optional<Datagram> udp_datagram(const std::span<const std::byte> frame) noexcept {
  const std::optional<Bytes> ip = ipv4_packet(frame);
  const std::optional<Bytes> packet = ip ? udp_packet(*ip) : std::nullopt;
  if (!packet) {
    return std::nullopt;
  }
  const std::size_t header = (u8(*packet, 0) & kLowNibble) * kWordSize;
  const Bytes segment = packet->subspan(header);
  static_cast<void>(check(segment.size() >= kUdpHeader));
  const std::size_t length = be16(segment, kUdpLengthAt);
  if (length < kUdpHeader || length > segment.size()) {
    return std::nullopt;
  }
  return Datagram{.source = endpoint(*packet, kSourceAt, segment, 0),
                  .destination = endpoint(*packet, kDestinationAt, segment, 2),
                  .payload = segment.subspan(kUdpHeader, length - kUdpHeader)};
}

}  // namespace ics::capture
