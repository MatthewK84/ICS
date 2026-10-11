#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "ics/common/error.hpp"
#include "ics/tagger/sampler.hpp"

namespace ics::tagger {

// One batch on the wire (ICS-032), sent to ics-mountd in a UDP datagram of
// exactly kDatagramBytes, little-endian:
//
//   offset  size  field
//        0     4  magic "ICST"
//        4     1  version, kDatagramVersion
//        5     1  samples, kBatchSamples
//        6     2  reserved, 0
//        8     4  sequence
//       12   240  kBatchSamples samples of 24 bytes:
//                   0  8  utc_ns, signed
//                   8  4  bound_ns, saturated at 2^32 - 1
//                  12  4  azimuth_counts, signed
//                  16  4  elevation_counts, signed
//                  20  1  state (ClockState)
//                  21  3  reserved, 0
//      252     4  CRC-32C of bytes 0 to 251
inline constexpr std::size_t kDatagramBytes = 256;
inline constexpr std::uint8_t kDatagramVersion = 1;

// The CRC-32C (Castagnoli) of bytes, as iSCSI and ext4 use it: reflected
// polynomial 0x82F63B78, initial value and final XOR 0xFFFFFFFF.
[[nodiscard]] std::uint32_t crc32c(std::span<const std::byte> bytes) noexcept;

// Writes batch to out, which must hold kDatagramBytes: the bytes written, or
// 0 when out is too small.
[[nodiscard]] std::size_t encode(const Batch& batch, std::span<std::byte> out) noexcept;

// Reads a datagram. Error::kMalformed for any other size, magic, version or
// sample count, a reserved byte that is not 0, a state that is not a
// ClockState, or a CRC that does not match.
[[nodiscard]] Result<Batch> decode(std::span<const std::byte> bytes) noexcept;

// An axis angle from its encoder counts, in radians from the axis zero, for
// an encoder of counts_per_rev counts a revolution; 0 for none.
[[nodiscard]] double angle_rad(std::int32_t counts, std::uint32_t counts_per_rev) noexcept;

}  // namespace ics::tagger
