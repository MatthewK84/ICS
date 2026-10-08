#include "ics/store/crc32c.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <gtest/gtest.h>

namespace {

std::span<const std::byte> bytes_of(const std::string_view text) { return std::as_bytes(std::span(text)); }

TEST(Crc32c, MatchesTheCheckValue) {
  // The check value of CRC-32C, from the catalogue of parametrised CRCs, and
  // RFC 3720's all-zero test vector.
  EXPECT_EQ(ics::store::crc32c(bytes_of("123456789")), 0xE3069283U);
  const std::array<std::byte, 32> zeros{};
  EXPECT_EQ(ics::store::crc32c(zeros), 0x8A9136AAU);
  EXPECT_EQ(ics::store::crc32c({}), 0U);
}

TEST(Crc32c, ExtendsAcrossPieces) {
  const std::uint32_t first = ics::store::crc32c(bytes_of("12345"));
  EXPECT_EQ(ics::store::crc32c_extend(first, bytes_of("6789")), 0xE3069283U);
}

}  // namespace
