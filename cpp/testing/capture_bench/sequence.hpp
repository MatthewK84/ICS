#pragma once

// What the capture bench's datagrams carry (ICS-020, deploy/capture-bench).

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <span>
#include <string_view>

namespace ics::capture_bench {

// Each datagram's payload: its sequence number, big-endian, then zeros.
inline constexpr std::size_t kPayloadBytes = 64;
inline constexpr std::size_t kSequenceBytes = 8;
// Where the sequence number sits in a captured frame: after the Ethernet,
// IPv4 and UDP headers.
inline constexpr std::size_t kSequenceOffset = 14 + 20 + 8;
inline constexpr unsigned kBitsPerByte = 8;

// Writes sequence, big-endian, over the first kSequenceBytes of out.
inline void put_sequence(const std::span<std::byte, kSequenceBytes> out, const std::uint64_t sequence) noexcept {
  for (std::size_t i = 0; i < kSequenceBytes; ++i) {
    out[i] = static_cast<std::byte>((sequence >> (kBitsPerByte * (kSequenceBytes - 1 - i))) & 0xFFU);
  }
}

// The big-endian sequence number in bytes.
[[nodiscard]] inline std::uint64_t get_sequence(const std::span<const std::byte, kSequenceBytes> bytes) noexcept {
  return std::accumulate(bytes.begin(), bytes.end(), std::uint64_t{0}, [](const std::uint64_t out, const std::byte byte) {
    return (out << kBitsPerByte) | std::to_integer<std::uint64_t>(byte);
  });
}

// text as a whole decimal number, or nothing.
[[nodiscard]] inline std::optional<std::uint64_t> number(const std::string_view text) noexcept {
  std::uint64_t out = 0;
  const std::from_chars_result read = std::from_chars(text.begin(), text.end(), out);
  if (read.ec != std::errc() || read.ptr != text.end() || text.empty()) {
    return std::nullopt;
  }
  return out;
}

}  // namespace ics::capture_bench
