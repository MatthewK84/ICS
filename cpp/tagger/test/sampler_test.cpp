#include "ics/tagger/sampler.hpp"

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "ics/tagger/pps_clock.hpp"
#include "ics/testing/no_allocation_scope.hpp"

namespace {

using ics::tagger::ClockState;
using ics::tagger::kBatchSamples;
using ics::tagger::PpsClock;
using ics::tagger::Sampler;

constexpr std::uint64_t kHz = 200'000'000;
constexpr std::int64_t kSecond = 1'790'000'000;

PpsClock locked_clock() {
  PpsClock clock(ics::tagger::ClockConfig{.nominal_hz = kHz});
  static_cast<void>(clock.on_pps(0));
  static_cast<void>(clock.label(kSecond, 10));
  static_cast<void>(clock.on_pps(kHz));
  static_cast<void>(clock.on_pps(2 * kHz));
  return clock;
}

TEST(TaggerSampler, FillsABatchOfTenSamples) {
  const PpsClock clock = locked_clock();
  Sampler sampler;
  for (std::size_t i = 0; i + 1 < kBatchSamples; ++i) {
    EXPECT_FALSE(sampler.add(clock, (2 * kHz) + (i * (kHz / 1000)), static_cast<std::int32_t>(i), -7));
  }
  EXPECT_TRUE(sampler.add(clock, (2 * kHz) + (9 * (kHz / 1000)), 9, -7));
  const ics::tagger::Batch& batch = sampler.batch();
  EXPECT_EQ(batch.sequence, 0U);
  for (std::size_t i = 0; i < kBatchSamples; ++i) {
    EXPECT_EQ(batch.samples[i].utc_ns, ((kSecond + 2) * 1'000'000'000) + static_cast<std::int64_t>(i * 1'000'000));
    EXPECT_EQ(batch.samples[i].bound_ns, 1'000);
    EXPECT_EQ(batch.samples[i].azimuth_counts, static_cast<std::int32_t>(i));
    EXPECT_EQ(batch.samples[i].elevation_counts, -7);
    EXPECT_EQ(batch.samples[i].state, ClockState::kLocked);
  }
}

TEST(TaggerSampler, NumbersEachBatch) {
  const PpsClock clock = locked_clock();
  Sampler sampler;
  const ics::testing::NoAllocationScope no_allocation;
  for (std::size_t i = 0; i < 3 * kBatchSamples; ++i) {
    static_cast<void>(sampler.add(clock, 2 * kHz, 0, 0));
  }
  EXPECT_EQ(sampler.batch().sequence, 2U);
  EXPECT_FALSE(sampler.add(clock, 2 * kHz, 0, 0));
  EXPECT_EQ(sampler.batch().sequence, 3U);
}

TEST(TaggerSampler, MarksSamplesTakenWhileAcquiring) {
  const PpsClock clock(ics::tagger::ClockConfig{.nominal_hz = kHz});
  Sampler sampler;
  EXPECT_FALSE(sampler.add(clock, 5, 1, 2));
  EXPECT_EQ(sampler.batch().samples[0].state, ClockState::kAcquiring);
  EXPECT_EQ(sampler.batch().samples[0].utc_ns, 0);
  EXPECT_EQ(sampler.batch().samples[0].azimuth_counts, 1);
}

}  // namespace
