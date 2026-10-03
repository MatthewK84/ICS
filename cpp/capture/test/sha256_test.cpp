#include "ics/capture/sha256.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"

namespace {

using ics::capture::Digest;
using ics::capture::Sha256;

std::vector<std::byte> bytes_of(const std::string_view text) {
  std::vector<std::byte> out;
  std::ranges::transform(text, std::back_inserter(out), [](const char c) { return static_cast<std::byte>(c); });
  return out;
}

std::string hex(const ics::Result<Digest>& digest) {
  EXPECT_TRUE(digest.has_value());
  const ics::capture::DigestHex text = ics::capture::to_hex(digest.value_or(Digest{}));
  return {text.begin(), text.end()};
}

// FIPS 180-4 examples (NIST CSRC): the empty message, "abc" and the 448-bit message.
TEST(Sha256, MatchesTheNistExamples) {
  Sha256 hash = Sha256::make().value();
  EXPECT_EQ(hex(hash.finish()), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  hash.update(bytes_of("abc"));
  EXPECT_EQ(hex(hash.finish()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  hash.update(bytes_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
  EXPECT_EQ(hex(hash.finish()), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, GivesTheSameDigestFedInPieces) {
  Sha256 hash = Sha256::make().value();
  for (const std::byte b : bytes_of("abc")) {
    hash.update({&b, 1});
  }
  Sha256 moved(std::move(hash));
  EXPECT_EQ(hex(moved.finish()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

}  // namespace
