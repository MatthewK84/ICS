#include "ics/capture/pcap_file.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ics::capture {
namespace {

constexpr std::uint32_t kNanosecondMagic = 0xA1B23C4DU;
constexpr std::uint16_t kVersionMajor = 2;
constexpr std::uint16_t kVersionMinor = 4;
constexpr std::int64_t kNanosPerSecond = 1'000'000'000;
constexpr unsigned kBitsPerByte = 8;
constexpr unsigned kByteMask = 0xFFU;

// value at out[at], least significant byte first.
template <std::size_t N>
void put(std::array<std::byte, N>& out, const std::size_t at, const std::uint32_t value,
         const std::size_t bytes) noexcept {
  for (std::size_t i = 0; i < bytes; ++i) {
    out[at + i] = static_cast<std::byte>((value >> (kBitsPerByte * i)) & kByteMask);
  }
}

}  // namespace

FileHeader file_header(const std::uint32_t snaplen, const std::uint32_t link_type) noexcept {
  // magic, version 2.4, time zone 0 and accuracy 0 (both always 0), snaplen,
  // link type.
  FileHeader out{};
  put(out, 0, kNanosecondMagic, 4);
  put(out, 4, kVersionMajor, 2);
  put(out, 6, kVersionMinor, 2);
  put(out, 16, snaplen, 4);
  put(out, 20, link_type, 4);
  return out;
}

RecordHeader record_header(const Packet& packet) noexcept {
  // Seconds, nanoseconds, bytes kept, bytes on the wire.
  const std::int64_t utc_ns = std::max<std::int64_t>(to_utc_ns(packet.time), 0);
  RecordHeader out{};
  put(out, 0, static_cast<std::uint32_t>(utc_ns / kNanosPerSecond), 4);
  put(out, 4, static_cast<std::uint32_t>(utc_ns % kNanosPerSecond), 4);
  put(out, 8, static_cast<std::uint32_t>(packet.data.size()), 4);
  put(out, 12, packet.wire_length, 4);
  return out;
}

}  // namespace ics::capture
