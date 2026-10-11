#include "ics/tagger/datagram.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>

#include "ics/common/check.hpp"

namespace ics::tagger {
namespace {

constexpr std::array<std::byte, 4> kMagic{std::byte{'I'}, std::byte{'C'}, std::byte{'S'}, std::byte{'T'}};
constexpr std::size_t kVersionAt = 4;
constexpr std::size_t kCountAt = 5;
constexpr std::size_t kReservedAt = 6;
constexpr std::size_t kSequenceAt = 8;
constexpr std::size_t kHeaderBytes = 12;
constexpr std::size_t kSampleBytes = 24;
constexpr std::size_t kCrcAt = kHeaderBytes + (kBatchSamples * kSampleBytes);
static_assert(kCrcAt + 4 == kDatagramBytes);

// A sample's fields, by offset in its 24 bytes.
constexpr std::size_t kUtcAt = 0;
constexpr std::size_t kBoundAt = 8;
constexpr std::size_t kAzimuthAt = 12;
constexpr std::size_t kElevationAt = 16;
constexpr std::size_t kStateAt = 20;
constexpr std::size_t kSampleReservedAt = 21;
constexpr std::uint64_t kMaxBound = std::numeric_limits<std::uint32_t>::max();

constexpr std::uint32_t kPolynomial = 0x82F63B78U;
constexpr std::uint32_t kCrcInit = 0xFFFFFFFFU;
constexpr unsigned kBitsPerByte = 8;

// Writes the size lowest bytes of value at out[at], lowest first.
void put(const std::span<std::byte> out, const std::size_t at, const std::uint64_t value,
         const std::size_t size) noexcept {
  for (std::size_t i = 0; i < size; ++i) {
    out[at + i] = static_cast<std::byte>(value >> (kBitsPerByte * i));
  }
}

// Reads size bytes at in[at], lowest first.
[[nodiscard]] std::uint64_t get(const std::span<const std::byte> in, const std::size_t at,
                                const std::size_t size) noexcept {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < size; ++i) {
    value |= std::to_integer<std::uint64_t>(in[at + i]) << (kBitsPerByte * i);
  }
  return value;
}

void put_sample(const Sample& sample, const std::span<std::byte> out) noexcept {
  const auto bound = static_cast<std::uint64_t>(std::max<std::int64_t>(sample.bound_ns, 0));
  put(out, kUtcAt, static_cast<std::uint64_t>(sample.utc_ns), 8);
  put(out, kBoundAt, std::min(bound, kMaxBound), 4);
  put(out, kAzimuthAt, static_cast<std::uint32_t>(sample.azimuth_counts), 4);
  put(out, kElevationAt, static_cast<std::uint32_t>(sample.elevation_counts), 4);
  put(out, kStateAt, static_cast<std::uint8_t>(sample.state), 1);
  put(out, kSampleReservedAt, 0, 3);
}

[[nodiscard]] Result<Sample> read_sample(const std::span<const std::byte> in) noexcept {
  const std::uint64_t state = get(in, kStateAt, 1);
  if (state > static_cast<std::uint8_t>(ClockState::kHoldover) || get(in, kSampleReservedAt, 3) != 0) {
    return fail(Error::kMalformed);
  }
  return Sample{.utc_ns = static_cast<std::int64_t>(get(in, kUtcAt, 8)),
                .bound_ns = static_cast<std::int64_t>(get(in, kBoundAt, 4)),
                .azimuth_counts = static_cast<std::int32_t>(static_cast<std::uint32_t>(get(in, kAzimuthAt, 4))),
                .elevation_counts = static_cast<std::int32_t>(static_cast<std::uint32_t>(get(in, kElevationAt, 4))),
                .state = static_cast<ClockState>(state)};
}

// Whether a datagram's size, header and CRC are as encode writes them.
[[nodiscard]] bool framed(const std::span<const std::byte> bytes) noexcept {
  if (bytes.size() != kDatagramBytes || !std::ranges::equal(bytes.first(kMagic.size()), kMagic)) {
    return false;
  }
  const bool header = get(bytes, kVersionAt, 1) == kDatagramVersion && get(bytes, kCountAt, 1) == kBatchSamples &&
                      get(bytes, kReservedAt, 2) == 0;
  return header && get(bytes, kCrcAt, 4) == crc32c(bytes.first(kCrcAt));
}

}  // namespace

std::uint32_t crc32c(const std::span<const std::byte> bytes) noexcept {
  std::uint32_t crc = kCrcInit;
  for (const std::byte byte : bytes) {
    crc ^= std::to_integer<std::uint32_t>(byte);
    for (unsigned bit = 0; bit < kBitsPerByte; ++bit) {
      // All ones when the low bit is set, so the polynomial goes in without a
      // branch.
      const std::uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (kPolynomial & mask);
    }
  }
  return ~crc;
}

std::size_t encode(const Batch& batch, const std::span<std::byte> out) noexcept {
  if (out.size() < kDatagramBytes) {
    return 0;
  }
  std::ranges::copy(kMagic, out.begin());
  put(out, kVersionAt, kDatagramVersion, 1);
  put(out, kCountAt, kBatchSamples, 1);
  put(out, kReservedAt, 0, 2);
  put(out, kSequenceAt, batch.sequence, 4);
  for (std::size_t i = 0; i < kBatchSamples; ++i) {
    put_sample(batch.samples[i], out.subspan(kHeaderBytes + (i * kSampleBytes), kSampleBytes));
  }
  put(out, kCrcAt, crc32c(out.first(kCrcAt)), 4);
  return kDatagramBytes;
}

Result<Batch> decode(const std::span<const std::byte> bytes) noexcept {
  if (!framed(bytes)) {
    return fail(Error::kMalformed);
  }
  Batch out{.sequence = static_cast<std::uint32_t>(get(bytes, kSequenceAt, 4)), .samples = {}};
  for (std::size_t i = 0; i < kBatchSamples; ++i) {
    const Result<Sample> sample = read_sample(bytes.subspan(kHeaderBytes + (i * kSampleBytes), kSampleBytes));
    if (!sample) {
      return fail(sample.error());
    }
    out.samples[i] = *sample;
  }
  // framed checked the size, so every sample was in it.
  static_cast<void>(ics::check(bytes.size() == kDatagramBytes));
  return out;
}

double angle_rad(const std::int32_t counts, const std::uint32_t counts_per_rev) noexcept {
  if (counts_per_rev == 0) {
    return 0.0;
  }
  return 2.0 * std::numbers::pi * static_cast<double>(counts) / static_cast<double>(counts_per_rev);
}

}  // namespace ics::tagger
