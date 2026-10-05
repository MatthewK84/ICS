#include "ics/timealign/check.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/timealign/clock_fit.hpp"

namespace ics::timealign {
namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;

// 2026-10-05T00:00:00Z.
constexpr std::int64_t kEpochNs = 1'791'158'400'000'000'000;
constexpr std::int64_t kSecondUs = 1'000'000;

// A clock without drift read once a second, as SITL's are, and the boot times
// of positions ten times a second over the same span. A boot time in whole
// milliseconds, as MAVLink gives it, is up to 1 ms late.
struct Sortie {
  std::vector<ClockSample> samples;
  std::vector<std::int64_t> positions;
};

Sortie sortie(const std::int64_t first_us, const std::int64_t seconds_long, const bool whole_ms) {
  Sortie out;
  for (std::int64_t s = 0; s < seconds_long; ++s) {
    // Read at a varying point in its millisecond.
    const std::int64_t boot_us = first_us + (s * kSecondUs) + 123'000 + ((s * 7919) % 1000);
    const std::int64_t reported = whole_ms ? boot_us - (boot_us % 1000) : boot_us;
    out.samples.push_back(ClockSample{.boot_us = reported, .utc_ns = kEpochNs + (boot_us * 1000)});
  }
  for (std::int64_t tenth = 0; tenth < seconds_long * 10; ++tenth) {
    out.positions.push_back(first_us + (tenth * 100'000));
  }
  return out;
}

TEST(Injection, PutsADriftAndAnOffsetOnBootTimes) {
  const Injection injection{.ppm = 100.0, .offset = seconds(5)};
  EXPECT_EQ(injection.apply(0), 5 * kSecondUs);
  EXPECT_EQ(injection.apply(1'000 * kSecondUs), 5 * kSecondUs + 1'000'100'000);
  EXPECT_EQ(Injection{.offset = seconds(-1)}.apply(0), std::nullopt);
  EXPECT_EQ(injection.apply(kMaxBootUs), std::nullopt);
}

TEST(DriftCheck, AlignsWithinMicrosecondsThroughAGnssOutage) {
  const Sortie flight = sortie(10 * kSecondUs, 600, false);
  for (const double ppm : {100.0, -100.0}) {
    const Result<DriftCheck> check =
        check_injected_drift(flight.samples, flight.positions, Injection{.ppm = ppm, .offset = seconds(5)},
                             Withholding{.from = 0.2, .to = 0.8});
    ASSERT_TRUE(check.has_value()) << ppm;
    EXPECT_EQ(check->positions, 6000U);
    EXPECT_LT(check->max_error, std::chrono::microseconds(2)) << ppm;
    EXPECT_LT(check->reference_spread.count(), 1.0);
    // The middle 60 % is withheld.
    EXPECT_LE(check->fit.used, 241U);
    EXPECT_NEAR(check->fit.model.drift_ppm(), -ppm, 0.02);
  }
}

TEST(DriftCheck, AlignsWithinAMillisecondFromWholeMillisecondBootTimes) {
  const Sortie flight = sortie(40 * kSecondUs, 300, true);
  const Result<DriftCheck> check = check_injected_drift(
      flight.samples, flight.positions, Injection{.ppm = 100.0, .offset = seconds(3)}, Withholding{.from = 0.2, .to = 0.8});
  ASSERT_TRUE(check.has_value());
  EXPECT_LT(check->max_error, milliseconds(1));
  EXPECT_GT(check->reference_spread.count(), 100'000.0);
  EXPECT_LT(check->reference_spread.count(), 1'000'000.0);
}

// The flight's clock, made to run drift_ppm fast and to wander by up to
// wander_ms.
Sortie reclocked(Sortie flight, const double drift_ppm, const double wander_ms) {
  for (std::size_t i = 0; i < flight.samples.size(); ++i) {
    const double boot_ns = static_cast<double>(flight.samples[i].boot_us) * 1000.0;
    const double wander_ns = wander_ms * 1e6 * std::sin(static_cast<double>(i) / 6.0);
    flight.samples[i].utc_ns += std::llround((boot_ns * drift_ppm * 1e-6) + wander_ns);
  }
  return flight;
}

TEST(DriftCheck, JudgesOnlySortiesWhoseClockIsAStraightLine) {
  const Injection injection{.ppm = 100.0, .offset = seconds(5)};
  const Withholding outage{.from = 0.2, .to = 0.8};
  const Sortie flight = sortie(10 * kSecondUs, 300, false);
  const Result<DriftCheck> sound = check_injected_drift(flight.samples, flight.positions, injection, outage);
  ASSERT_TRUE(sound.has_value());
  EXPECT_EQ(sound->sent.used, 300U);
  EXPECT_EQ(judge(*sound), Outcome::kWithin);
  EXPECT_EQ(judge(*sound, std::chrono::nanoseconds(-1)), Outcome::kBeyond);
  EXPECT_EQ(judge(*sound, kAlignmentLimit, Nanoseconds(-1.0)), Outcome::kNotStraight);
  // A straight clock with a drift of its own breaks the reference, which takes
  // it to have none: the check applies and fails, rather than passing it by.
  const Sortie drifting = reclocked(flight, 50.0, 0.0);
  const Result<DriftCheck> broken = check_injected_drift(drifting.samples, drifting.positions, injection, outage);
  ASSERT_TRUE(broken.has_value());
  EXPECT_TRUE(straight(broken->sent));
  EXPECT_GT(broken->max_error, milliseconds(1));
  EXPECT_EQ(judge(*broken), Outcome::kBeyond);
  // A clock that wanders, as PX4 SIH's does, is not checked.
  const Sortie wandering = reclocked(flight, -25'000.0, 20.0);
  const Result<DriftCheck> uneven = check_injected_drift(wandering.samples, wandering.positions, injection, outage);
  ASSERT_TRUE(uneven.has_value());
  EXPECT_EQ(judge(*uneven), Outcome::kNotStraight);
}

TEST(DriftCheck, FailsWhenTheSamplesOrTheInjectionCannotBeFitted) {
  const Sortie flight = sortie(10 * kSecondUs, 60, false);
  const Injection injection{.ppm = 100.0, .offset = seconds(5)};
  const Withholding none{};
  const std::vector<ClockSample> two(flight.samples.begin(), flight.samples.begin() + 2);
  EXPECT_EQ(check_injected_drift(two, flight.positions, injection, none).error(), Error::kEmpty);
  EXPECT_EQ(check_injected_drift(flight.samples, flight.positions, Injection{.offset = seconds(-20)}, none).error(),
            Error::kInvalidArgument);
  EXPECT_EQ(check_injected_drift(flight.samples, flight.positions, injection, Withholding{.from = 0.0, .to = 1.0})
                .error(),
            Error::kEmpty);
}

TEST(DriftCheck, FailsForAPositionOutsideTheTimesIcsTakes) {
  const Sortie flight = sortie(10 * kSecondUs, 60, false);
  const Injection injection{.ppm = 0.0, .offset = seconds(5)};
  const Withholding none{};
  for (const std::int64_t boot_us : {std::int64_t{-1}, kMaxBootUs}) {
    const std::vector<std::int64_t> positions{boot_us};
    EXPECT_EQ(check_injected_drift(flight.samples, positions, injection, none).error(), Error::kInvalidArgument)
        << boot_us;
  }
  // A boot time past those ICS take that the injection brings back in range.
  std::vector<ClockSample> late;
  for (std::int64_t s = 0; s < 60; ++s) {
    late.push_back({.boot_us = kMaxBootUs - ((100 - s) * kSecondUs), .utc_ns = kEpochNs + (s * kSecondUs * 1000)});
  }
  const std::vector<std::int64_t> past{kMaxBootUs + 1};
  EXPECT_EQ(check_injected_drift(late, past, Injection{.offset = seconds(-10)}, none).error(),
            Error::kInvalidArgument);
}

}  // namespace
}  // namespace ics::timealign
