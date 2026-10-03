#include "ics/capture/sha256.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using ics::capture::Sha256;

std::span<const std::byte> bytes_of(const std::string_view text) { return std::as_bytes(std::span(text)); }

// The hash of text, fed in pieces of piece bytes.
std::string hash_in_pieces(const std::string_view text, const std::size_t piece) {
  Sha256 hash = Sha256::make().value();
  for (std::size_t at = 0; at < text.size(); at += piece) {
    EXPECT_TRUE(hash.update(bytes_of(text.substr(at, piece))).has_value());
  }
  return ics::capture::to_hex(hash.finish().value());
}

// FIPS 180-2 Appendix B's two-block example.
constexpr std::string_view kTwoBlocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

TEST(Sha256, MatchesTheNistExamples) {
  EXPECT_EQ(hash_in_pieces("", 1), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(hash_in_pieces("abc", 3), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(hash_in_pieces(kTwoBlocks, 7), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, RefusesBytesAndASecondFinishAfterFinishingUntilRestarted) {
  Sha256 hash = Sha256::make().value();
  ASSERT_TRUE(hash.finish().has_value());
  EXPECT_EQ(hash.update(bytes_of("abc")).error(), ics::Error::kUnavailable);
  EXPECT_EQ(hash.finish().error(), ics::Error::kUnavailable);
  ASSERT_TRUE(hash.restart().has_value());
  ASSERT_TRUE(hash.update(bytes_of("abc")).has_value());
  EXPECT_EQ(ics::capture::to_hex(hash.finish().value()),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

}  // namespace
