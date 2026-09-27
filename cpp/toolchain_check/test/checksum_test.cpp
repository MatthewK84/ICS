#include "ics/toolchain_check/checksum.hpp"

#include <cstdint>
#include <span>
#include <string_view>

#include <gtest/gtest.h>

namespace {

std::uint32_t hash_of(std::string_view text) {
  return ics::toolchain_check::fnv1a32(std::as_bytes(std::span{text}));
}

TEST(Fnv1a32, MatchesPublishedVectors) {
  EXPECT_EQ(hash_of(""), 0x811c9dc5U);
  EXPECT_EQ(hash_of("a"), 0xe40c292cU);
  EXPECT_EQ(hash_of("foobar"), 0xbf9cf968U);
}

TEST(Fnv1a32, DependsOnByteOrder) {
  EXPECT_NE(hash_of("ab"), hash_of("ba"));
}

}  // namespace
