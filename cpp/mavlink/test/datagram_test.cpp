#include "ics/mavlink/datagram.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "mavlink_support.hpp"

namespace ics::mavlink {
namespace {

using testing::Bytes;
using testing::ethernet;
using testing::from_hex;
using testing::ipv4_udp;

const Bytes kPayload = from_hex(testing::kHeartbeat);

Bytes payload_of(const std::optional<Datagram>& datagram) {
  return datagram ? Bytes(datagram->payload.begin(), datagram->payload.end()) : Bytes{};
}

// The frame with one byte of its IPv4 packet replaced.
Bytes with_ip_byte(const std::size_t at, const unsigned value) {
  Bytes frame = ethernet(ipv4_udp(kPayload));
  frame.at(14 + at) = static_cast<std::byte>(value);
  return frame;
}

TEST(UdpDatagram, ReadsTheAddressesPortsAndPayload) {
  // The datagram refers to the frame's bytes, so the frame must outlive it.
  const Bytes frame = ethernet(ipv4_udp(kPayload));
  const std::optional<Datagram> datagram = udp_datagram(frame);
  ASSERT_TRUE(datagram.has_value());
  EXPECT_EQ(datagram->source.address, (std::array<std::uint8_t, 4>{172, 30, 18, 10}));
  EXPECT_EQ(datagram->source.port, 18570);
  EXPECT_EQ(datagram->destination.address, (std::array<std::uint8_t, 4>{172, 30, 18, 1}));
  EXPECT_EQ(datagram->destination.port, 14551);
  EXPECT_EQ(payload_of(datagram), kPayload);
}

TEST(UdpDatagram, ReadsATaggedFrame) {
  EXPECT_EQ(payload_of(udp_datagram(ethernet(ipv4_udp(kPayload), true))), kPayload);
}

TEST(UdpDatagram, IgnoresEthernetPadding) {
  const Bytes small{std::byte{0xFE}};
  Bytes frame = ethernet(ipv4_udp(small));
  frame.resize(60, std::byte{0});
  EXPECT_EQ(payload_of(udp_datagram(frame)), small);
}

TEST(UdpDatagram, IgnoresWhatIsNotUdpOverIpv4) {
  EXPECT_FALSE(udp_datagram(Bytes(13, std::byte{0})).has_value());
  EXPECT_FALSE(udp_datagram(ethernet(ipv4_udp(kPayload), false, 0x0806)).has_value());
  EXPECT_FALSE(udp_datagram(ethernet(ipv4_udp(kPayload), true, 0x86DD)).has_value());
  Bytes tag_only = ethernet({}, true);
  tag_only.resize(17);
  EXPECT_FALSE(udp_datagram(tag_only).has_value());
  EXPECT_FALSE(udp_datagram(with_ip_byte(9, 6)).has_value());  // TCP
}

TEST(UdpDatagram, RejectsBadIpv4Headers) {
  EXPECT_FALSE(udp_datagram(ethernet(Bytes(19, std::byte{0x45}))).has_value());
  EXPECT_FALSE(udp_datagram(with_ip_byte(0, 0x65)).has_value());  // version 6
  EXPECT_FALSE(udp_datagram(with_ip_byte(0, 0x44)).has_value());  // a 16-byte header
  Bytes short_total = ethernet(ipv4_udp(kPayload));
  short_total.at(14 + 2) = std::byte{0};
  short_total.at(14 + 3) = std::byte{27};
  EXPECT_FALSE(udp_datagram(short_total).has_value());
}

TEST(UdpDatagram, RejectsFragmentsAndPacketsCutShort) {
  EXPECT_FALSE(udp_datagram(with_ip_byte(6, 0x20)).has_value());  // more fragments
  EXPECT_FALSE(udp_datagram(with_ip_byte(7, 0x01)).has_value());  // a non-zero offset
  Bytes cut = ethernet(ipv4_udp(kPayload));
  cut.pop_back();
  EXPECT_FALSE(udp_datagram(cut).has_value());
}

TEST(UdpDatagram, RejectsABadUdpLength) {
  EXPECT_FALSE(udp_datagram(with_ip_byte(20 + 5, 7)).has_value());  // shorter than its header
  Bytes long_udp = ethernet(ipv4_udp(kPayload));
  long_udp.at(14 + 20 + 5) = std::byte{0xFF};  // longer than the packet
  EXPECT_FALSE(udp_datagram(long_udp).has_value());
}

}  // namespace
}  // namespace ics::mavlink
