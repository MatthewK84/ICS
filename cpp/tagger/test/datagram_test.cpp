#include "ics/tagger/datagram.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string_view>

#include <gtest/gtest.h>

#include "ics/testing/no_allocation_scope.hpp"

namespace {

using ics::tagger::Batch;
using ics::tagger::ClockState;
using ics::tagger::kDatagramBytes;
using Datagram = std::array<std::byte, kDatagramBytes>;

constexpr std::size_t kCrcAt = 252;
constexpr std::size_t kFirstSample = 12;

Batch batch() {
  Batch out{.sequence = 0xA1B2'C3D4U, .samples = {}};
  for (std::size_t i = 0; i < out.samples.size(); ++i) {
    out.samples[i] = ics::tagger::Sample{.utc_ns = 1'790'000'000'000'000'000 + static_cast<std::int64_t>(i),
                                         .bound_ns = 1'000 * static_cast<std::int64_t>(i),
                                         .azimuth_counts = -2'000'000'000 + static_cast<std::int32_t>(i),
                                         .elevation_counts = 2'000'000'000 - static_cast<std::int32_t>(i),
                                         .state = static_cast<ClockState>(i % 3)};
  }
  return out;
}

Datagram encoded(const Batch& in) {
  Datagram out{};
  EXPECT_EQ(ics::tagger::encode(in, out), kDatagramBytes);
  return out;
}

// Writes the CRC of a datagram edited after encoding.
Datagram resealed(Datagram bytes) {
  const std::uint32_t crc = ics::tagger::crc32c(std::span(bytes).first(kCrcAt));
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[kCrcAt + i] = static_cast<std::byte>(crc >> (8U * i));
  }
  return bytes;
}

// The datagram with one byte changed, and its CRC written again.
Datagram edited(const std::size_t at, const std::byte value) {
  Datagram bytes = encoded(batch());
  bytes[at] = value;
  return resealed(bytes);
}

void expect_malformed(const std::span<const std::byte> bytes) {
  const auto decoded = ics::tagger::decode(bytes);
  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error(), ics::Error::kMalformed);
}

TEST(TaggerDatagram, ComputesCrc32c) {
  constexpr std::string_view kCheck = "123456789";
  EXPECT_EQ(ics::tagger::crc32c(std::as_bytes(std::span(kCheck))), 0xE306'9283U);
  EXPECT_EQ(ics::tagger::crc32c({}), 0U);
}

TEST(TaggerDatagram, ReadsBackWhatItWrites) {
  const Batch in = batch();
  const Datagram bytes = encoded(in);
  EXPECT_EQ(bytes[0], std::byte{'I'});
  EXPECT_EQ(bytes[3], std::byte{'T'});
  const auto out = ics::tagger::decode(bytes);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->sequence, in.sequence);
  for (std::size_t i = 0; i < in.samples.size(); ++i) {
    EXPECT_EQ(out->samples[i].utc_ns, in.samples[i].utc_ns);
    EXPECT_EQ(out->samples[i].bound_ns, in.samples[i].bound_ns);
    EXPECT_EQ(out->samples[i].azimuth_counts, in.samples[i].azimuth_counts);
    EXPECT_EQ(out->samples[i].elevation_counts, in.samples[i].elevation_counts);
    EXPECT_EQ(out->samples[i].state, in.samples[i].state);
  }
}

TEST(TaggerDatagram, SaturatesTheBound) {
  Batch in = batch();
  in.samples[0].bound_ns = 5'000'000'000;
  in.samples[1].bound_ns = -1;
  const auto out = ics::tagger::decode(encoded(in));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->samples[0].bound_ns, 4'294'967'295);
  EXPECT_EQ(out->samples[1].bound_ns, 0);
}

TEST(TaggerDatagram, WritesNothingIntoTooSmallABuffer) {
  std::array<std::byte, kDatagramBytes - 1> small{};
  EXPECT_EQ(ics::tagger::encode(batch(), small), 0U);
}

TEST(TaggerDatagram, RefusesAnythingElse) {
  const Datagram good = encoded(batch());
  expect_malformed(std::span(good).first(kDatagramBytes - 1));
  const std::array<std::byte, kDatagramBytes + 1> longer{};
  expect_malformed(longer);
  expect_malformed(edited(0, std::byte{'X'}));
  expect_malformed(edited(4, std::byte{2}));
  expect_malformed(edited(5, std::byte{9}));
  expect_malformed(edited(7, std::byte{1}));
  // A sample's state that is not a ClockState, and its reserved bytes.
  expect_malformed(edited(kFirstSample + 20, std::byte{3}));
  expect_malformed(edited(kFirstSample + 23, std::byte{1}));
  // A byte changed without its CRC.
  Datagram corrupt = good;
  corrupt[100] ^= std::byte{1};
  expect_malformed(corrupt);
}

TEST(TaggerDatagram, AllocatesNothing) {
  const Batch in = batch();
  Datagram bytes{};
  const ics::testing::NoAllocationScope no_allocation;
  EXPECT_EQ(ics::tagger::encode(in, bytes), kDatagramBytes);
  EXPECT_TRUE(ics::tagger::decode(bytes).has_value());
}

TEST(TaggerDatagram, TurnsCountsIntoAnAngle) {
  EXPECT_DOUBLE_EQ(ics::tagger::angle_rad(1 << 20, 1U << 22U), std::numbers::pi / 2.0);
  EXPECT_DOUBLE_EQ(ics::tagger::angle_rad(-(1 << 21), 1U << 22U), -std::numbers::pi);
  EXPECT_EQ(ics::tagger::angle_rad(123, 0), 0.0);
}

}  // namespace
