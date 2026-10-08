#include "ics/store/crc32c.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ics::store {

namespace {

// The reflected Castagnoli polynomial.
constexpr std::uint32_t kPolynomial = 0x82F63B78U;
constexpr std::size_t kTableSize = 256;
constexpr int kBitsPerByte = 8;
constexpr std::uint32_t kLowByte = 0xFFU;

// The CRC of each byte value, one bit at a time, worked out while compiling.
constexpr std::array<std::uint32_t, kTableSize> kTable = []() consteval {
  std::array<std::uint32_t, kTableSize> table{};
  for (std::size_t value = 0; value < kTableSize; ++value) {
    auto crc = static_cast<std::uint32_t>(value);
    for (int bit = 0; bit < kBitsPerByte; ++bit) {
      crc = (crc & 1U) != 0U ? (crc >> 1U) ^ kPolynomial : crc >> 1U;
    }
    table.at(value) = crc;
  }
  return table;
}();

}  // namespace

std::uint32_t crc32c_extend(const std::uint32_t crc, const std::span<const std::byte> bytes) noexcept {
  std::uint32_t state = ~crc;
  for (const std::byte byte : bytes) {
    const std::uint32_t index = (state ^ std::to_integer<std::uint32_t>(byte)) & kLowByte;
    state = (state >> static_cast<unsigned>(kBitsPerByte)) ^ kTable[index];
  }
  return ~state;
}

std::uint32_t crc32c(const std::span<const std::byte> bytes) noexcept { return crc32c_extend(0, bytes); }

}  // namespace ics::store
