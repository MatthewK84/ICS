#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ics::store {

// CRC-32C (Castagnoli, polynomial 0x1EDC6F41, reflected), the checksum of
// each segment log entry (ICS-030). It detects a torn or corrupt entry, not
// tampering. crc32c of "123456789" is 0xE3069283.
[[nodiscard]] std::uint32_t crc32c(std::span<const std::byte> bytes) noexcept;

// Continues a CRC-32C: crc32c(a then b) is crc32c_extend(crc32c(a), b).
[[nodiscard]] std::uint32_t crc32c_extend(std::uint32_t crc, std::span<const std::byte> bytes) noexcept;

}  // namespace ics::store
