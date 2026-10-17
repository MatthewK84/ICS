// Axis angles from the tagger's samples, and their interpolation between
// samples.
#include "ics/encoder/track.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <numbers>

#include <gtest/gtest.h>

#include "encoder_support.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/encoder/angles.hpp"
#include "ics/tagger/pps_clock.hpp"
#include "ics/tagger/sampler.hpp"

namespace {

using ics::encoder::AngleTrack;
using ics::encoder::EncoderConfig;
using ics::tagger::ClockState;
using namespace ics::encoder::testing;

constexpr double kTiny = 1e-12;
constexpr std::int32_t kQuarterTurn = 1 << 22;

[[nodiscard]] ics::UtcTime at_ns(const std::int64_t ns) { return ics::utc_from_ns(ns); }

[[nodiscard]] AngleTrack track_of(const std::size_t capacity = ics::encoder::kDefaultHistory,
                                  const EncoderConfig& encoders = config()) {
  return AngleTrack::make(encoders, capacity).value();
}

TEST(Angles, TurnsCountsIntoAxisAngles) {
  const ics::tagger::Sample sample{.utc_ns = kStartNs,
                                   .bound_ns = kLockedBoundNs,
                                   .azimuth_counts = kQuarterTurn,
                                   .elevation_counts = kQuarterTurn / 2,
                                   .state = ClockState::kHoldover};
  const auto angles = ics::encoder::angles_from(sample, config());
  ASSERT_TRUE(angles.has_value());
  EXPECT_EQ(angles->utc, at_ns(kStartNs));
  EXPECT_EQ(angles->bound, ics::Duration(kLockedBoundNs));
  EXPECT_NEAR(angles->encoder.azimuth_rad, std::numbers::pi / 2, kTiny);
  EXPECT_NEAR(angles->encoder.elevation_rad, std::numbers::pi / 4, kTiny);
  EXPECT_EQ(angles->state, ClockState::kHoldover);
}

TEST(Angles, ReadsReversedAndWrappedEncoders) {
  const ics::tagger::Sample sample{.utc_ns = kStartNs,
                                   .azimuth_counts = kQuarterTurn,
                                   .elevation_counts = static_cast<std::int32_t>(kCountsPerRev) - 1,
                                   .state = ClockState::kLocked};
  const auto reversed = ics::encoder::angles_from(sample, config(true, false));
  ASSERT_TRUE(reversed.has_value());
  EXPECT_NEAR(reversed->encoder.azimuth_rad, 3 * std::numbers::pi / 2, kTiny);
  // One count short of a turn is one count below the horizon.
  EXPECT_NEAR(reversed->encoder.elevation_rad, -kCountRad, kTiny);
  const auto both = ics::encoder::angles_from(sample, config(true, true));
  EXPECT_NEAR(both->encoder.elevation_rad, kCountRad, kTiny);
}

TEST(Angles, RefusesASampleWithoutATimeOrEncodersWithoutCounts) {
  const ics::tagger::Sample acquiring{};
  EXPECT_EQ(ics::encoder::angles_from(acquiring, config()).error(), ics::Error::kUnconstrained);
  const ics::tagger::Sample locked{.utc_ns = kStartNs, .state = ClockState::kLocked};
  EncoderConfig no_azimuth = config();
  no_azimuth.azimuth.counts_per_rev = 1;
  EXPECT_FALSE(ics::encoder::valid(no_azimuth));
  EXPECT_EQ(ics::encoder::angles_from(locked, no_azimuth).error(), ics::Error::kInvalidArgument);
  EncoderConfig no_elevation = config();
  no_elevation.elevation.counts_per_rev = 0;
  EXPECT_EQ(ics::encoder::angles_from(locked, no_elevation).error(), ics::Error::kInvalidArgument);
  EXPECT_TRUE(ics::encoder::valid(config()));
}

TEST(Angles, NamesTheClockStates) {
  EXPECT_EQ(ics::encoder::to_string(ClockState::kAcquiring), "acquiring");
  EXPECT_EQ(ics::encoder::to_string(ClockState::kLocked), "locked");
  EXPECT_EQ(ics::encoder::to_string(ClockState::kHoldover), "holdover");
  EXPECT_EQ(ics::encoder::to_string(static_cast<ClockState>(3)), "unknown");
}

TEST(AngleTrack, RefusesABadConfigOrTooSmallAHistory) {
  EncoderConfig bad = config();
  bad.elevation.counts_per_rev = 1;
  EXPECT_EQ(AngleTrack::make(bad).error(), ics::Error::kInvalidArgument);
  EXPECT_EQ(AngleTrack::make(config(), 1).error(), ics::Error::kInvalidArgument);
  EXPECT_TRUE(AngleTrack::make(config(), 2).has_value());
}

TEST(AngleTrack, KnowsNothingUntilItHasTwoSamples) {
  AngleTrack track = track_of();
  EXPECT_EQ(track.newest(), std::nullopt);
  EXPECT_EQ(track.at(at_ns(kStartNs)).error(), ics::Error::kOutOfRange);
  ics::tagger::Batch one = batch({});
  for (std::size_t i = 1; i < one.samples.size(); ++i) {
    one.samples.at(i) = ics::tagger::Sample{};
  }
  track.add(one);
  EXPECT_EQ(track.size(), 1U);
  ASSERT_TRUE(track.newest().has_value());
  EXPECT_EQ(track.newest()->utc, at_ns(kStartNs));
  EXPECT_EQ(track.at(at_ns(kStartNs)).error(), ics::Error::kOutOfRange);
  EXPECT_EQ(track.stats().acquiring, ics::tagger::kBatchSamples - 1);
}

TEST(AngleTrack, InterpolatesAnglesAndRatesBetweenSamples) {
  AngleTrack track = track_of();
  // 100 counts a millisecond in azimuth, -40 in elevation.
  track.add(batch({.azimuth = 1000, .azimuth_step = 100, .elevation = kQuarterTurn / 2, .elevation_step = -40}));
  const auto mid = track.at(at_ns(kStartNs + (3 * kSampleNs) + (kSampleNs / 4)));
  ASSERT_TRUE(mid.has_value());
  EXPECT_NEAR(mid->encoder.azimuth_rad, (1000 + 325) * kCountRad, kTiny);
  EXPECT_NEAR(mid->encoder.elevation_rad, std::numbers::pi / 4 - (130 * kCountRad), kTiny);
  EXPECT_NEAR(mid->azimuth_rate_rad_per_s, 100 * kCountRad * 1000, 1e-9);
  EXPECT_NEAR(mid->elevation_rate_rad_per_s, -40 * kCountRad * 1000, 1e-9);
  EXPECT_EQ(mid->time_bound, ics::Duration(kLockedBoundNs));
  EXPECT_EQ(mid->state, ClockState::kLocked);
  const double cos_e = std::cos(mid->encoder.elevation_rad);
  const double sky_rate = std::hypot(mid->azimuth_rate_rad_per_s * cos_e, mid->elevation_rate_rad_per_s);
  EXPECT_NEAR(mid->bound_rad, (kLockedBoundNs * 1e-9 * sky_rate) + std::hypot(kCountRad * cos_e, kCountRad), 1e-15);
}

TEST(AngleTrack, InterpolatesAtTheOldestAndNewestSamples) {
  AngleTrack track = track_of();
  track.add(batch({.azimuth = 0, .azimuth_step = 10}));
  EXPECT_NEAR(track.at(at_ns(kStartNs))->encoder.azimuth_rad, 0.0, kTiny);
  EXPECT_NEAR(track.at(at_ns(kStartNs + (9 * kSampleNs)))->encoder.azimuth_rad, 90 * kCountRad, kTiny);
  EXPECT_EQ(track.at(at_ns(kStartNs - 1)).error(), ics::Error::kOutOfRange);
  EXPECT_EQ(track.at(at_ns(kStartNs + (9 * kSampleNs) + 1)).error(), ics::Error::kOutOfRange);
}

TEST(AngleTrack, InterpolatesAcrossTheAzimuthEncodersWrap) {
  AngleTrack track = track_of();
  const auto turn = static_cast<std::int32_t>(kCountsPerRev);
  ics::tagger::Batch wrapping = batch({.azimuth = turn - 5, .azimuth_step = 2});
  for (ics::tagger::Sample& sample : wrapping.samples) {
    sample.azimuth_counts %= turn;
  }
  track.add(wrapping);
  // From turn - 1 to turn + 1, which the encoder reads as 1.
  const auto across = track.at(at_ns(kStartNs + (2 * kSampleNs) + (kSampleNs / 2)));
  ASSERT_TRUE(across.has_value());
  EXPECT_NEAR(ics::encoder::wrap_pi(across->encoder.azimuth_rad), 0.0, kTiny);
  EXPECT_NEAR(across->azimuth_rate_rad_per_s, 2 * kCountRad * 1000, 1e-9);
  const auto after = track.at(at_ns(kStartNs + (2 * kSampleNs) + (3 * kSampleNs / 4)));
  EXPECT_NEAR(after->encoder.azimuth_rad, 0.5 * kCountRad, kTiny);
}

TEST(AngleTrack, RefusesATimeInAGapOfMoreThanTenMilliseconds) {
  AngleTrack track = track_of();
  track.add(batch({.sequence = 0}));
  // The next batch's first sample is 10 ms after this one's last: allowed.
  track.add(batch({.sequence = 2, .first_ns = kStartNs + (19 * kSampleNs)}));
  EXPECT_TRUE(track.at(at_ns(kStartNs + (14 * kSampleNs))).has_value());
  // One nanosecond more is refused.
  track.add(batch({.sequence = 4, .first_ns = kStartNs + (38 * kSampleNs) + 1}));
  EXPECT_EQ(track.at(at_ns(kStartNs + (33 * kSampleNs))).error(), ics::Error::kUnconstrained);
  EXPECT_EQ(track.stats().lost_batches, 2U);
  EXPECT_EQ(track.stats().batches, 3U);
}

TEST(AngleTrack, DropsSamplesWithoutATimeAndRefusesTheGapTheyLeave) {
  AngleTrack track = track_of();
  track.add(batch({.sequence = 0}));
  track.add(batch({.sequence = 1, .first_ns = kStartNs + kBatchNs, .state = ClockState::kAcquiring}));
  track.add(batch({.sequence = 2, .first_ns = kStartNs + (2 * kBatchNs)}));
  EXPECT_EQ(track.stats().acquiring, ics::tagger::kBatchSamples);
  EXPECT_EQ(track.stats().samples, 2 * ics::tagger::kBatchSamples);
  EXPECT_EQ(track.at(at_ns(kStartNs + (15 * kSampleNs))).error(), ics::Error::kUnconstrained);
}

TEST(AngleTrack, CarriesHoldoverAndTheLargerBound) {
  AngleTrack track = track_of();
  track.add(batch({.sequence = 0}));
  track.add(batch({.sequence = 1, .first_ns = kStartNs + kBatchNs, .state = ClockState::kHoldover, .bound_ns = 9000}));
  const auto edge = track.at(at_ns(kStartNs + (9 * kSampleNs) + (kSampleNs / 2)));
  ASSERT_TRUE(edge.has_value());
  EXPECT_EQ(edge->state, ClockState::kHoldover);
  EXPECT_EQ(edge->time_bound, ics::Duration(9000));
  EXPECT_EQ(track.at(at_ns(kStartNs + kSampleNs))->state, ClockState::kLocked);
}

TEST(AngleTrack, CountsSequenceResetsAndDropsSamplesOutOfOrder) {
  AngleTrack track = track_of();
  track.add(batch({.sequence = 7}));
  // The same batch again: its sequence went back, and its samples are old.
  track.add(batch({.sequence = 7}));
  EXPECT_EQ(track.stats().sequence_resets, 1U);
  EXPECT_EQ(track.stats().out_of_order, ics::tagger::kBatchSamples);
  // A tagger that starts again from sequence 0, with later samples.
  track.add(batch({.sequence = 0, .first_ns = kStartNs + kBatchNs}));
  EXPECT_EQ(track.stats().sequence_resets, 2U);
  EXPECT_EQ(track.stats().lost_batches, 0U);
  EXPECT_EQ(track.size(), 2 * ics::tagger::kBatchSamples);
}

TEST(AngleTrack, CountsNoLossWhereTheSequenceNumberWraps) {
  AngleTrack track = track_of();
  track.add(batch({.sequence = 0xFFFF'FFFEU}));
  track.add(batch({.sequence = 0xFFFF'FFFFU, .first_ns = kStartNs + kBatchNs}));
  track.add(batch({.sequence = 0, .first_ns = kStartNs + (2 * kBatchNs)}));
  track.add(batch({.sequence = 2, .first_ns = kStartNs + (3 * kBatchNs)}));
  EXPECT_EQ(track.stats().lost_batches, 1U);
  EXPECT_EQ(track.stats().sequence_resets, 0U);
}

TEST(AngleTrack, KeepsOnlyTheNewestSamples) {
  AngleTrack track = track_of(25);
  for (std::uint32_t i = 0; i < 5; ++i) {
    track.add(batch({.sequence = i,
                     .first_ns = kStartNs + (static_cast<std::int64_t>(i) * kBatchNs),
                     .azimuth = static_cast<std::int32_t>(i) * 10,
                     .azimuth_step = 1}));
  }
  EXPECT_EQ(track.size(), 25U);
  EXPECT_EQ(track.stats().samples, 50U);
  EXPECT_EQ(track.at(at_ns(kStartNs + (24 * kSampleNs))).error(), ics::Error::kOutOfRange);
  EXPECT_NEAR(track.at(at_ns(kStartNs + (25 * kSampleNs)))->encoder.azimuth_rad, 25 * kCountRad, kTiny);
  EXPECT_NEAR(track.at(at_ns(kStartNs + (49 * kSampleNs)))->encoder.azimuth_rad, 49 * kCountRad, kTiny);
  EXPECT_EQ(track.newest()->utc, at_ns(kStartNs + (49 * kSampleNs)));
}

}  // namespace
