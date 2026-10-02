#include "ics/timing/clock_state.hpp"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "ics/timing/ptp_management.hpp"

namespace {

using ics::timing::ClockQuality;
using ics::timing::ClockState;
using ics::timing::PortState;
using ics::timing::TimePropertiesDataSet;

constexpr std::uint8_t kTraceable = 0x3C;    // the flags of a grandmaster locked to GNSS
constexpr std::uint8_t kUntraceable = 0x2C;  // the same, without timeTraceable

ClockState classify(const PortState port, const std::uint8_t clock_class, const std::uint8_t flags) {
  return ics::timing::classify(port, ClockQuality{clock_class, 0x21, 0x4E5D}, TimePropertiesDataSet{0, flags, 0x20});
}

TEST(ClockState, LocksOnlyToATraceablePrimaryReference) {
  EXPECT_EQ(classify(PortState::kSlave, 6, kTraceable), ClockState::kLocked);
  // ptp4l's own default grandmaster claims class 6 without traceable time.
  EXPECT_EQ(classify(PortState::kSlave, 6, kUntraceable), ClockState::kFreeRunning);
}

TEST(ClockState, RecognizesEveryHoldoverClass) {
  for (const std::uint8_t clock_class : std::to_array<std::uint8_t>({7, 135, 140, 150, 160})) {
    EXPECT_EQ(classify(PortState::kSlave, clock_class, kUntraceable), ClockState::kHoldover) << int{clock_class};
  }
}

TEST(ClockState, CallsEveryOtherClassFreeRunning) {
  for (const std::uint8_t clock_class : std::to_array<std::uint8_t>({0, 5, 8, 13, 52, 134, 136, 139, 161, 165, 187, 248, 255})) {
    EXPECT_EQ(classify(PortState::kSlave, clock_class, kTraceable), ClockState::kFreeRunning) << int{clock_class};
  }
}

TEST(ClockState, FollowsNoGrandmasterUnlessThePortIsSlave) {
  for (const PortState port : {PortState::kListening, PortState::kUncalibrated, PortState::kMaster,
                               PortState::kFaulty}) {
    EXPECT_EQ(classify(port, 6, kTraceable), ClockState::kFreeRunning);
  }
}

TEST(ClockState, NamesEachState) {
  EXPECT_EQ(ics::timing::to_string(ClockState::kLocked), "locked");
  EXPECT_EQ(ics::timing::to_string(ClockState::kHoldover), "holdover");
  EXPECT_EQ(ics::timing::to_string(ClockState::kFreeRunning), "free_running");
  EXPECT_EQ(ics::timing::to_string(static_cast<ClockState>(0)), "unknown");
}

}  // namespace
