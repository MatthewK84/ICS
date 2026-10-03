#include "ics/timing/quality.hpp"

#include <array>
#include <chrono>
#include <cstdint>

#include <gtest/gtest.h>

#include "ics/common/units.hpp"
#include "ics/testing/no_allocation_scope.hpp"
#include "ics/timing/clock_state.hpp"
#include "ics/timing/ptp_management.hpp"

namespace {

using ics::Duration;
using ics::timing::ClockState;
using ics::timing::ErrorModel;
using ics::timing::kUnbounded;
using ics::timing::PortState;
using ics::timing::Quality;
using ics::timing::QualityTracker;
using ics::timing::Snapshot;
using ics::timing::SteadyTime;
using std::chrono::seconds;

constexpr ErrorModel kModel{Duration(1'000), 50.0};
constexpr std::uint8_t kWithin100Ns = 0x21;

// A poll of a station following a grandmaster of this class.
Snapshot snapshot(const std::uint8_t clock_class, const std::uint8_t accuracy = kWithin100Ns,
                  const PortState port = PortState::kSlave) {
  Snapshot polled;
  polled.port.port_state = port;
  polled.current.offset_from_master = Duration(-191);
  polled.current.mean_path_delay = Duration(1455);
  polled.parent.grandmaster_quality = {clock_class, accuracy, 0x4E5D};
  polled.time.flags = clock_class == 6 ? 0x3C : 0x2C;
  return polled;
}

SteadyTime at(const std::int64_t s) { return SteadyTime(seconds(s)); }

TEST(AccuracyBound, ReadsTheStandardsTable) {
  EXPECT_EQ(ics::timing::accuracy_bound(0x17), Duration(1));      // 1 ps, rounded up
  EXPECT_EQ(ics::timing::accuracy_bound(0x1E), Duration(3));      // 2.5 ns, rounded up
  EXPECT_EQ(ics::timing::accuracy_bound(0x21), Duration(100));
  EXPECT_EQ(ics::timing::accuracy_bound(0x23), Duration(1'000));
  EXPECT_EQ(ics::timing::accuracy_bound(0x30), seconds(10));
}

TEST(AccuracyBound, HasNoBoundForUnknownOrReservedValues) {
  for (const std::uint8_t accuracy : std::to_array<std::uint8_t>({0x00, 0x16, 0x31, 0x80, 0xFE, 0xFF})) {
    EXPECT_EQ(ics::timing::accuracy_bound(accuracy), kUnbounded) << int{accuracy};
  }
}

TEST(QualityTracker, StartsFreeRunningWithNoBound) {
  const QualityTracker tracker(kModel);
  EXPECT_EQ(tracker.state(), ClockState::kFreeRunning);
  const Quality quality = tracker.quality(at(0));
  EXPECT_EQ(quality.state, ClockState::kFreeRunning);
  EXPECT_EQ(quality.error_bound, kUnbounded);
}

TEST(QualityTracker, BoundsALockedClockByOffsetAccuracyAndAsymmetry) {
  QualityTracker tracker(kModel);
  EXPECT_TRUE(tracker.update(snapshot(6), at(1)));
  EXPECT_FALSE(tracker.update(snapshot(6), at(2)));
  const Quality quality = tracker.quality(at(2));
  EXPECT_EQ(quality.state, ClockState::kLocked);
  EXPECT_EQ(quality.holdover, Duration(0));
  EXPECT_EQ(quality.ptp_offset, Duration(-191));
  EXPECT_EQ(quality.ptp_path_delay, Duration(1455));
  EXPECT_EQ(quality.error_bound, Duration(191 + 100 + 1'000));
}

TEST(QualityTracker, TimesHoldoverFromTheFirstPollThatShowsIt) {
  QualityTracker tracker(kModel);
  static_cast<void>(tracker.update(snapshot(6), at(0)));
  EXPECT_TRUE(tracker.update(snapshot(7), at(10)));
  EXPECT_FALSE(tracker.update(snapshot(7), at(20)));
  const Quality quality = tracker.quality(at(30));
  EXPECT_EQ(quality.state, ClockState::kHoldover);
  EXPECT_EQ(quality.holdover, seconds(20));
  // 20 s at 50 ns/s adds 1000 ns of drift.
  EXPECT_EQ(quality.error_bound, Duration(191 + 100 + 1'000 + 1'000));
  // Locking again ends holdover, and a new one is timed afresh.
  EXPECT_TRUE(tracker.update(snapshot(6), at(31)));
  EXPECT_EQ(tracker.quality(at(31)).holdover, Duration(0));
  EXPECT_TRUE(tracker.update(snapshot(7), at(40)));
  EXPECT_EQ(tracker.quality(at(41)).holdover, seconds(1));
}

TEST(QualityTracker, LosingPtp4lMakesTheClockFreeRunning) {
  QualityTracker tracker(kModel);
  static_cast<void>(tracker.update(snapshot(6), at(0)));
  EXPECT_TRUE(tracker.lost(at(1)));
  EXPECT_FALSE(tracker.lost(at(2)));
  const Quality quality = tracker.quality(at(2));
  EXPECT_EQ(quality.state, ClockState::kFreeRunning);
  EXPECT_EQ(quality.ptp_offset, Duration(0));
  EXPECT_EQ(quality.ptp_path_delay, Duration(0));
  EXPECT_EQ(quality.error_bound, kUnbounded);
}

TEST(QualityTracker, HasNoBoundWhenTheAccuracyIsUnknownOrTheDriftOverflows) {
  QualityTracker unknown(kModel);
  static_cast<void>(unknown.update(snapshot(6, 0xFE), at(0)));
  EXPECT_EQ(unknown.quality(at(0)).state, ClockState::kLocked);
  EXPECT_EQ(unknown.quality(at(0)).error_bound, kUnbounded);
  QualityTracker drifting(ErrorModel{Duration(0), 1e300});
  static_cast<void>(drifting.update(snapshot(7), at(0)));
  EXPECT_EQ(drifting.quality(at(1)).error_bound, kUnbounded);
}

TEST(QualityTracker, AllocatesNothing) {
  QualityTracker tracker(kModel);
  const Snapshot locked = snapshot(6);
  const ics::testing::NoAllocationScope no_allocation;
  static_cast<void>(tracker.update(locked, at(0)));
  EXPECT_EQ(tracker.quality(at(1)).state, ClockState::kLocked);
}

}  // namespace
