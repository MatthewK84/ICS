#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ics/common/units.hpp"

namespace ics::capture {

// libpcap's savefile format ("classic pcap") with nanosecond time stamps
// (magic 0xa1b23c4d), written little-endian (ICS-020). Readers such as
// libpcap, tcpdump and Wireshark detect the byte order from the magic.

inline constexpr std::size_t kFileHeaderSize = 24;
inline constexpr std::size_t kRecordHeaderSize = 16;
// LINKTYPE_ETHERNET, the link type of a TAP port.
inline constexpr std::uint32_t kLinkEthernet = 1;

using FileHeader = std::array<std::byte, kFileHeaderSize>;
using RecordHeader = std::array<std::byte, kRecordHeaderSize>;

// One captured packet: when it arrived, its length on the wire, and the
// bytes kept, at most the snapshot length.
struct Packet {
  UtcTime time;
  std::uint32_t wire_length = 0;
  std::span<const std::byte> data;
};

// The header that starts a file of packets cut to snaplen bytes.
[[nodiscard]] FileHeader file_header(std::uint32_t snaplen, std::uint32_t link_type) noexcept;

// The header that precedes packet's bytes. A time before 1970 is written as
// 1970-01-01T00:00:00Z, which the format's unsigned seconds cannot go below.
[[nodiscard]] RecordHeader record_header(const Packet& packet) noexcept;

}  // namespace ics::capture
