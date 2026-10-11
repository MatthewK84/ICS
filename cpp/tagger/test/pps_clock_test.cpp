#include "ics/tagger/pps_clock.hpp"

#include <cmath>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "ics/testing/no_allocation_scope.hpp"

namespace {

using ics::tagger::ClockConfig;
using ics::tagger::ClockState;
using ics::tagger::LabelResult;
using ics::tagger::PpsClock;
using ics::tagger::PpsResult;
using ics::tagger::Tag;

constexpr std::uint64_t kNominalHz = 200'000'000;
// The counter's true rate: 20 ppm fast.
constexpr std::uint64_t kTrueHz = 200'004'000;
constexpr std::int64_t kSecond = 1'790'000'000;
constexpr std::int64_t kNsPerSecond = 1'000'000'000;

ClockConfig config() { return ClockConfig{.nominal_hz = kNominalHz}; }

// A clock that has taken edges at 0, 1 and 2 s, and the label kSecond for the
// first.
PpsClock locked_clock() {
  PpsClock clock(config());
  EXPECT_EQ(clock.on_pps(0), PpsResult::kFirst);
  EXPECT_EQ(clock.label(kSecond, kTrueHz / 5), LabelResult::kApplied);
  EXPECT_EQ(clock.on_pps(kTrueHz), PpsResult::kAccepted);
  EXPECT_EQ(clock.on_pps(2 * kTrueHz), PpsResult::kAccepted);
  return clock;
}

TEST(TaggerPpsClock, LocksOnThreeEdgesAndALabel) {
  PpsClock clock(config());
  EXPECT_EQ(clock.tag(0).state, ClockState::kAcquiring);
  EXPECT_EQ(clock.on_pps(0), PpsResult::kFirst);
  EXPECT_EQ(clock.on_pps(kTrueHz), PpsResult::kAccepted);
  EXPECT_EQ(clock.on_pps(2 * kTrueHz), PpsResult::kAccepted);
  // Three edges, but no label yet.
  const Tag unlabeled = clock.tag((2 * kTrueHz) + 10);
  EXPECT_EQ(unlabeled.state, ClockState::kAcquiring);
  EXPECT_EQ(unlabeled.utc_ns, 0);
  EXPECT_EQ(unlabeled.bound_ns, 0);
  EXPECT_EQ(clock.label(kSecond + 2, (2 * kTrueHz) + (kTrueHz / 5)), LabelResult::kApplied);
  const Tag tag = clock.tag((2 * kTrueHz) + (kTrueHz / 2));
  EXPECT_EQ(tag.state, ClockState::kLocked);
  EXPECT_NEAR(static_cast<double>(tag.utc_ns), static_cast<double>(((kSecond + 2) * kNsPerSecond) + 500'000'000), 1.0);
  EXPECT_EQ(tag.bound_ns, 1'000);
  EXPECT_NEAR(clock.rate_hz(), static_cast<double>(kTrueHz), 1e-3);
}

TEST(TaggerPpsClock, NeedsThreeEdgesInARow) {
  PpsClock clock(config());
  EXPECT_EQ(clock.on_pps(0), PpsResult::kFirst);
  EXPECT_EQ(clock.label(kSecond, 1000), LabelResult::kApplied);
  EXPECT_EQ(clock.on_pps(kTrueHz), PpsResult::kAccepted);
  EXPECT_EQ(clock.tag(kTrueHz + 10).state, ClockState::kAcquiring);
}

TEST(TaggerPpsClock, CountsSecondsOnFromTheLabel) {
  PpsClock clock = locked_clock();
  EXPECT_EQ(clock.tag(2 * kTrueHz).utc_ns, (kSecond + 2) * kNsPerSecond);
  EXPECT_EQ(clock.on_pps(3 * kTrueHz), PpsResult::kAccepted);
  EXPECT_EQ(clock.tag(3 * kTrueHz).utc_ns, (kSecond + 3) * kNsPerSecond);
  // A reading latched just before the edge it is stamped after.
  EXPECT_EQ(clock.tag((3 * kTrueHz) - 200).utc_ns, ((kSecond + 3) * kNsPerSecond) - 1000);
}

TEST(TaggerPpsClock, TakesTheHostsLabel) {
  PpsClock clock(config());
  EXPECT_EQ(clock.label(kSecond, 0), LabelResult::kRejected);
  EXPECT_EQ(clock.on_pps(kTrueHz), PpsResult::kFirst);
  // Before the edge, and more than half a second after it.
  EXPECT_EQ(clock.label(kSecond, kTrueHz - 1), LabelResult::kRejected);
  EXPECT_EQ(clock.label(kSecond, kTrueHz + (kTrueHz / 2) + 1000), LabelResult::kRejected);
  EXPECT_EQ(clock.label(kSecond, kTrueHz + (kTrueHz / 2) - 10'000), LabelResult::kApplied);
  EXPECT_EQ(clock.label(kSecond, kTrueHz + 10), LabelResult::kConfirmed);
  EXPECT_EQ(clock.on_pps(2 * kTrueHz), PpsResult::kAccepted);
  EXPECT_EQ(clock.label(kSecond + 1, (2 * kTrueHz) + 10), LabelResult::kConfirmed);
  EXPECT_EQ(clock.label(kSecond + 7, (2 * kTrueHz) + 10), LabelResult::kCorrected);
  EXPECT_EQ(clock.on_pps(3 * kTrueHz), PpsResult::kAccepted);
  EXPECT_EQ(clock.tag(3 * kTrueHz).utc_ns, (kSecond + 8) * kNsPerSecond);
}

TEST(TaggerPpsClock, RejectsGlitches) {
  PpsClock clock = locked_clock();
  // Too soon, half a second on, and the same edge again.
  EXPECT_EQ(clock.on_pps((2 * kTrueHz) + (kTrueHz / 3)), PpsResult::kRejected);
  EXPECT_EQ(clock.on_pps((2 * kTrueHz) + (kTrueHz / 2)), PpsResult::kRejected);
  EXPECT_EQ(clock.on_pps(3 * kTrueHz), PpsResult::kAccepted);
  EXPECT_EQ(clock.on_pps(3 * kTrueHz), PpsResult::kRejected);
  // 200 µs late is outside the 100 µs window.
  EXPECT_EQ(clock.on_pps((4 * kTrueHz) + (kTrueHz / 5000)), PpsResult::kRejected);
  EXPECT_EQ(clock.on_pps(4 * kTrueHz), PpsResult::kAccepted);
  EXPECT_EQ(clock.tag(4 * kTrueHz).state, ClockState::kLocked);
}

TEST(TaggerPpsClock, StartsAgainAfterThreeRejectsInARow) {
  PpsClock clock = locked_clock();
  const std::uint64_t moved = (2 * kTrueHz) + (kTrueHz / 4);
  EXPECT_EQ(clock.on_pps(moved + kTrueHz), PpsResult::kRejected);
  EXPECT_EQ(clock.on_pps(moved + (2 * kTrueHz)), PpsResult::kRejected);
  EXPECT_EQ(clock.on_pps(moved + (3 * kTrueHz)), PpsResult::kRestarted);
  // The label went with the old edges; the rate stays.
  EXPECT_EQ(clock.tag(moved + (3 * kTrueHz)).state, ClockState::kAcquiring);
  EXPECT_NEAR(clock.rate_hz(), static_cast<double>(kTrueHz), 1e-3);
  EXPECT_EQ(clock.on_pps(moved + (4 * kTrueHz)), PpsResult::kAccepted);
}

TEST(TaggerPpsClock, AcceptsTheFirstSecondWithinTheCrystalsTolerance) {
  PpsClock fast(config());
  EXPECT_EQ(fast.on_pps(0), PpsResult::kFirst);
  // 40 ppm fast: inside 50 ppm and the window.
  EXPECT_EQ(fast.on_pps(200'008'000), PpsResult::kAccepted);
  PpsClock faster(config());
  EXPECT_EQ(faster.on_pps(0), PpsResult::kFirst);
  // 200 ppm fast: outside.
  EXPECT_EQ(faster.on_pps(200'040'000), PpsResult::kRejected);
}

TEST(TaggerPpsClock, HoldsOverWhenAnEdgeIsMissed) {
  PpsClock clock = locked_clock();
  // 50 µs late is still locked: within the window.
  EXPECT_EQ(clock.tag((3 * kTrueHz) + (kTrueHz / 20'000)).state, ClockState::kLocked);
  const Tag holdover = clock.tag((5 * kTrueHz) + (kTrueHz / 2));
  EXPECT_EQ(holdover.state, ClockState::kHoldover);
  EXPECT_EQ(holdover.bound_ns, 1'000 + 5'000);
  EXPECT_EQ(holdover.utc_ns, ((kSecond + 5) * kNsPerSecond) + 500'000'000);
  // The edges come back four seconds on.
  EXPECT_EQ(clock.on_pps(6 * kTrueHz), PpsResult::kAccepted);
  const Tag back = clock.tag(6 * kTrueHz);
  EXPECT_EQ(back.state, ClockState::kLocked);
  EXPECT_EQ(back.utc_ns, (kSecond + 6) * kNsPerSecond);
}

TEST(TaggerPpsClock, TakesAConfigWithoutARateAsOneHertz) {
  testing::internal::CaptureStderr();
  const PpsClock clock(ClockConfig{});
  EXPECT_NE(testing::internal::GetCapturedStderr().find("ICS check failed"), std::string::npos);
  EXPECT_EQ(clock.rate_hz(), 1.0);
}

TEST(TaggerPpsClock, AllocatesNothing) {
  PpsClock clock(config());
  const ics::testing::NoAllocationScope no_allocation;
  static_cast<void>(clock.on_pps(0));
  static_cast<void>(clock.label(kSecond, 10));
  static_cast<void>(clock.on_pps(kTrueHz));
  static_cast<void>(clock.on_pps(2 * kTrueHz));
  EXPECT_EQ(clock.tag(2 * kTrueHz).state, ClockState::kLocked);
}

}  // namespace
