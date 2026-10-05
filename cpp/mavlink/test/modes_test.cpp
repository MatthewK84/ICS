#include "ics/mavlink/modes.hpp"

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

#include "ics/mavlink/messages.hpp"

namespace ics::mavlink {
namespace {

constexpr std::uint8_t kGeneric = 0;
constexpr std::uint8_t kArduPilot = 3;
constexpr std::uint8_t kPx4 = 12;
constexpr std::uint8_t kFixedWing = 1;
constexpr std::uint8_t kQuadrotor = 2;
constexpr std::uint8_t kHexarotor = 13;

Heartbeat heartbeat(const std::uint8_t autopilot, const std::uint8_t type, const std::uint32_t custom_mode) {
  Heartbeat out;
  out.autopilot = autopilot;
  out.type = type;
  out.custom_mode = custom_mode;
  return out;
}

std::uint32_t px4(const std::uint32_t main_mode, const std::uint32_t sub_mode) {
  return (main_mode << 16U) | (sub_mode << 24U);
}

TEST(ModeName, NamesPx4MainModesAndSubModes) {
  EXPECT_EQ(mode_name(heartbeat(kPx4, kQuadrotor, px4(1, 0))), "MANUAL");
  EXPECT_EQ(mode_name(heartbeat(kPx4, kQuadrotor, px4(2, 5))), "ALTCTL");
  EXPECT_EQ(mode_name(heartbeat(kPx4, kQuadrotor, px4(3, 0))), "POSCTL");
  EXPECT_EQ(mode_name(heartbeat(kPx4, kQuadrotor, px4(3, 1))), "POSCTL.ORBIT");
  EXPECT_EQ(mode_name(heartbeat(kPx4, kQuadrotor, px4(4, 4))), "AUTO.MISSION");
  EXPECT_EQ(mode_name(heartbeat(kPx4, kQuadrotor, px4(4, 3))), "AUTO.LOITER");
  EXPECT_EQ(mode_name(heartbeat(kPx4, kQuadrotor, px4(4, 7))), "AUTO");
  EXPECT_EQ(mode_name(heartbeat(kPx4, kQuadrotor, px4(10, 0))), "TERMINATION");
}

TEST(ModeName, GivesTheNumberOfAPx4ModeItDoesNotKnow) {
  EXPECT_EQ(mode_name(heartbeat(kPx4, kQuadrotor, px4(9, 0))), "custom mode 589824");
}

TEST(ModeName, NamesArduCopterModes) {
  EXPECT_EQ(mode_name(heartbeat(kArduPilot, kQuadrotor, 0)), "STABILIZE");
  EXPECT_EQ(mode_name(heartbeat(kArduPilot, kQuadrotor, 4)), "GUIDED");
  EXPECT_EQ(mode_name(heartbeat(kArduPilot, kHexarotor, 5)), "LOITER");
  EXPECT_EQ(mode_name(heartbeat(kArduPilot, kQuadrotor, 28)), "TURTLE");
}

TEST(ModeName, NamesArduCopterModesByNumber) {
  EXPECT_EQ(copter_mode_name(5), "LOITER");
  EXPECT_EQ(copter_mode_name(3), "AUTO");
  EXPECT_EQ(copter_mode_name(8), std::nullopt);
}

TEST(ModeName, GivesTheNumberForOtherVehiclesAndAutopilots) {
  EXPECT_EQ(mode_name(heartbeat(kArduPilot, kQuadrotor, 8)), "custom mode 8");
  EXPECT_EQ(mode_name(heartbeat(kArduPilot, kFixedWing, 5)), "custom mode 5");
  EXPECT_EQ(mode_name(heartbeat(kGeneric, kQuadrotor, 3)), "custom mode 3");
}

}  // namespace
}  // namespace ics::mavlink
