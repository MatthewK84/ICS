#include "ics/timealign/clock_fit.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::timealign {
namespace {

// 2026-10-05T00:00:00Z.
constexpr std::int64_t kEpochNs = 1'791'158'400'000'000'000;
constexpr std::int64_t kSecondUs = 1'000'000;

// UTC at a boot time, for a clock drift_ppm fast: the drift is small, so
// only it is rounded.
std::int64_t drifted_utc_ns(const std::int64_t boot_us, const double drift_ppm) {
  return kEpochNs + (boot_us * 1000) + std::llround(static_cast<double>(boot_us) * 1000.0 * drift_ppm * 1e-6);
}

// A clock whose UTC runs drift_ppm faster than its boot clock, read once a
// second from boot time 10 s for the given seconds.
std::vector<ClockSample> clock(const double drift_ppm, const std::int64_t seconds) {
  std::vector<ClockSample> out;
  for (std::int64_t s = 0; s < seconds; ++s) {
    const std::int64_t boot_us = (10 + s) * kSecondUs;
    out.push_back(ClockSample{.boot_us = boot_us, .utc_ns = drifted_utc_ns(boot_us, drift_ppm)});
  }
  return out;
}

std::int64_t utc_ns(const ClockFit& fit, const std::int64_t boot_us) { return to_utc_ns(fit.model.utc(boot_us).value()); }

TEST(ClockFit, RecoversAnOffsetAndADrift) {
  for (const double drift : {100.0, -100.0, 0.0}) {
    const std::vector<ClockSample> samples = clock(drift, 600);
    const Result<ClockFit> fit = fit_clock(samples);
    ASSERT_TRUE(fit.has_value()) << drift;
    EXPECT_NEAR(fit->model.drift_ppm(), drift, 1e-6);
    EXPECT_EQ(fit->used, 600U);
    EXPECT_EQ(fit->rejected, 0U);
    EXPECT_LT(fit->residual_max.count(), 1.0);
    EXPECT_EQ(fit->first_boot_us, 10 * kSecondUs);
    EXPECT_EQ(fit->last_boot_us, 609 * kSecondUs);
    for (const ClockSample& sample : samples) {
      EXPECT_NEAR(static_cast<double>(utc_ns(*fit, sample.boot_us) - sample.utc_ns), 0.0, 2.0);
    }
    // Two hours on, a drift unaccounted for would be 720 ms off.
    EXPECT_NEAR(static_cast<double>(utc_ns(*fit, 7200 * kSecondUs) - drifted_utc_ns(7200 * kSecondUs, drift)), 0.0,
                10.0);
  }
}

TEST(ClockFit, AveragesTheMillisecondResolutionOfBootTimes) {
  std::vector<ClockSample> samples = clock(50.0, 300);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    samples[i].boot_us += static_cast<std::int64_t>((i * 7919) % 1000);  // up to 1 ms late
  }
  const Result<ClockFit> fit = fit_clock(samples);
  ASSERT_TRUE(fit.has_value());
  EXPECT_EQ(fit->rejected, 0U);
  EXPECT_GT(fit->residual_rms.count(), 100'000.0);
  EXPECT_LT(fit->residual_max.count(), 1'000'000.0);
  EXPECT_NEAR(fit->model.drift_ppm(), 50.0, 1.0);
}

TEST(ClockFit, IsStraightOnlyWhenItsPairsLieOnALine) {
  const Result<ClockFit> line = fit_clock(clock(100.0, 120));
  ASSERT_TRUE(line.has_value());
  EXPECT_TRUE(straight(*line));
  // Read in whole milliseconds, a clock strays about 0.3 ms from its line.
  std::vector<ClockSample> rounded = clock(-30.0, 300);
  for (std::size_t i = 0; i < rounded.size(); ++i) {
    rounded[i].boot_us += static_cast<std::int64_t>((i * 7919) % 1000);
  }
  const Result<ClockFit> coarse = fit_clock(rounded);
  ASSERT_TRUE(coarse.has_value());
  EXPECT_GT(coarse->residual_rms.count(), 200'000.0);
  EXPECT_TRUE(straight(*coarse));
  EXPECT_FALSE(straight(*coarse, Nanoseconds(200'000.0)));
  // A simulated clock 2.5 % slow whose rate wanders, as PX4 SIH's does.
  std::vector<ClockSample> wandering = clock(-25'000.0, 150);
  for (std::size_t i = 0; i < wandering.size(); ++i) {
    wandering[i].utc_ns += std::llround(20'000'000.0 * std::sin(static_cast<double>(i) / 6.0));
  }
  const Result<ClockFit> uneven = fit_clock(wandering);
  ASSERT_TRUE(uneven.has_value());
  EXPECT_EQ(uneven->rejected, 0U);
  EXPECT_GT(uneven->residual_rms.count(), 10'000'000.0);
  EXPECT_FALSE(straight(*uneven));
}

TEST(ClockFit, LeavesOutOutliers) {
  std::vector<ClockSample> samples = clock(100.0, 120);
  for (const std::size_t i : {5U, 60U, 61U}) {
    samples[i].utc_ns += 50'000'000;  // a GNSS time 50 ms off
  }
  const Result<ClockFit> fit = fit_clock(samples);
  ASSERT_TRUE(fit.has_value());
  EXPECT_EQ(fit->used, 117U);
  EXPECT_EQ(fit->rejected, 3U);
  EXPECT_NEAR(fit->model.drift_ppm(), 100.0, 1e-3);
  EXPECT_LT(fit->residual_max.count(), 10.0);
  const Result<ClockFit> once = fit_clock(samples, FitSettings{.max_iterations = 0});
  ASSERT_TRUE(once.has_value());
  EXPECT_EQ(once->rejected, 0U);
  EXPECT_GT(once->residual_max.count(), 1'000'000.0);
}

TEST(ClockFit, NeedsEnoughSamplesAtMoreThanOneBootTime) {
  const std::vector<ClockSample> two = clock(0.0, 2);
  EXPECT_EQ(fit_clock(two).error(), Error::kEmpty);
  EXPECT_TRUE(fit_clock(two, FitSettings{.min_samples = 0}).has_value());
  const std::vector<ClockSample> same{{.boot_us = 5, .utc_ns = kEpochNs},
                                      {.boot_us = 5, .utc_ns = kEpochNs + 1},
                                      {.boot_us = 5, .utc_ns = kEpochNs + 2}};
  EXPECT_EQ(fit_clock(same).error(), Error::kEmpty);
  // Leaving out the outlier leaves nine of the ten needed.
  std::vector<ClockSample> ten = clock(0.0, 10);
  ten[4].utc_ns += 1'000'000'000;
  EXPECT_EQ(fit_clock(ten, FitSettings{.min_samples = 10}).error(), Error::kEmpty);
}

TEST(ClockFit, RefusesTimesOutsideThoseIcsTakes) {
  const std::vector<std::vector<ClockSample>> refused{
      {{.boot_us = -1, .utc_ns = kEpochNs}},
      {{.boot_us = kMaxBootUs + 1, .utc_ns = kEpochNs}},
      {{.boot_us = 0, .utc_ns = -1}},
      {{.boot_us = 0, .utc_ns = kMaxUtcNs + 1}}};
  for (const std::vector<ClockSample>& samples : refused) {
    std::vector<ClockSample> padded = clock(0.0, 3);
    padded.push_back(samples.front());
    EXPECT_EQ(fit_clock(padded).error(), Error::kInvalidArgument);
  }
}

TEST(ClockFit, RefusesAFitThatLeavesTheTimesIcsTakes) {
  const FitSettings plain{.max_iterations = 0};
  std::vector<ClockSample> low(5, ClockSample{.boot_us = 0, .utc_ns = 0});
  low.push_back({.boot_us = 1, .utc_ns = 0});
  low.push_back({.boot_us = 2, .utc_ns = kMaxUtcNs});
  EXPECT_EQ(fit_clock(low, plain).error(), Error::kInvalidArgument);
  std::vector<ClockSample> high(5, ClockSample{.boot_us = 2, .utc_ns = kMaxUtcNs});
  high.push_back({.boot_us = 1, .utc_ns = kMaxUtcNs});
  high.push_back({.boot_us = 0, .utc_ns = 0});
  EXPECT_EQ(fit_clock(high, plain).error(), Error::kInvalidArgument);
}

TEST(ClockModel, TimesOnlyBootTimesAndOriginsIcsTakes) {
  const ClockModel model(10 * kSecondUs, kEpochNs, 1e-4);
  EXPECT_EQ(model.origin_boot_us(), 10 * kSecondUs);
  EXPECT_EQ(model.origin_utc(), utc_from_ns(kEpochNs));
  EXPECT_DOUBLE_EQ(model.drift_ppm(), 100.0);
  EXPECT_EQ(model.utc(20 * kSecondUs), utc_from_ns(kEpochNs + 10'001'000'000));
  EXPECT_EQ(model.utc(-1), std::nullopt);
  EXPECT_EQ(model.utc(kMaxBootUs + 1), std::nullopt);
  for (const ClockModel& bad : {ClockModel(-1, kEpochNs, 0.0), ClockModel(kMaxBootUs + 1, kEpochNs, 0.0),
                                ClockModel(0, -1, 0.0), ClockModel(0, kMaxUtcNs + 1, 0.0)}) {
    EXPECT_EQ(bad.utc(0), std::nullopt);
  }
  // Times before 1970 or from 2100, or no time at all.
  EXPECT_EQ(ClockModel(kSecondUs, 0, 0.0).utc(0), std::nullopt);
  EXPECT_EQ(ClockModel(0, kMaxUtcNs, 0.0).utc(1), std::nullopt);
  EXPECT_EQ(ClockModel(0, 0, std::nan("")).utc(1), std::nullopt);
}

}  // namespace
}  // namespace ics::timealign
