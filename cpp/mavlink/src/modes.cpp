#include "ics/mavlink/modes.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "ics/mavlink/messages.hpp"

namespace ics::mavlink {
namespace {

constexpr std::uint8_t kAutopilotArduPilot = 3;
constexpr std::uint8_t kAutopilotPx4 = 12;
// PX4 packs its main mode into bits 16 to 23 of custom_mode and its sub-mode
// into bits 24 to 31 (px4_custom_mode.h).
constexpr unsigned kMainModeShift = 16;
constexpr unsigned kSubModeShift = 24;
constexpr unsigned kByteMask = 0xFFU;
constexpr std::uint32_t kPx4Auto = 4;
constexpr std::uint32_t kPx4Posctl = 3;

struct Named {
  std::uint32_t number = 0;
  std::string_view name;
};

// MAV_TYPE values ArduCopter flies: quadrotor, coaxial, helicopter, hexarotor,
// octorotor, tricopter and dodecarotor.
constexpr std::array<std::uint8_t, 7> kCopterTypes{2, 3, 4, 13, 14, 15, 29};

constexpr std::array<Named, 8> kPx4MainModes{{{1, "MANUAL"},
                                              {2, "ALTCTL"},
                                              {3, "POSCTL"},
                                              {4, "AUTO"},
                                              {5, "ACRO"},
                                              {6, "OFFBOARD"},
                                              {7, "STABILIZED"},
                                              {10, "TERMINATION"}}};

constexpr std::array<Named, 9> kPx4AutoModes{{{1, "READY"},
                                              {2, "TAKEOFF"},
                                              {3, "LOITER"},
                                              {4, "MISSION"},
                                              {5, "RTL"},
                                              {6, "LAND"},
                                              {8, "FOLLOW_TARGET"},
                                              {9, "PRECLAND"},
                                              {10, "VTOL_TAKEOFF"}}};

constexpr std::array<Named, 2> kPx4PosctlModes{{{1, "ORBIT"}, {2, "SLOW"}}};

// ArduCopter's flight modes (ArduCopter/mode.h, Copter 4.7).
constexpr std::array<Named, 26> kCopterModes{{{0, "STABILIZE"},    {1, "ACRO"},          {2, "ALT_HOLD"},
                                              {3, "AUTO"},         {4, "GUIDED"},        {5, "LOITER"},
                                              {6, "RTL"},          {7, "CIRCLE"},        {9, "LAND"},
                                              {11, "DRIFT"},       {13, "SPORT"},        {14, "FLIP"},
                                              {15, "AUTOTUNE"},    {16, "POSHOLD"},      {17, "BRAKE"},
                                              {18, "THROW"},       {19, "AVOID_ADSB"},   {20, "GUIDED_NOGPS"},
                                              {21, "SMART_RTL"},   {22, "FLOWHOLD"},     {23, "FOLLOW"},
                                              {24, "ZIGZAG"},      {25, "SYSTEMID"},     {26, "AUTOROTATE"},
                                              {27, "AUTO_RTL"},    {28, "TURTLE"}}};

[[nodiscard]] std::optional<std::string_view> find(const std::span<const Named> table, const std::uint32_t number) {
  const auto found = std::ranges::find_if(table, [number](const Named& entry) { return entry.number == number; });
  if (found == table.end()) {
    return std::nullopt;
  }
  return found->name;
}

[[nodiscard]] std::optional<std::string> px4_mode(const std::uint32_t custom_mode) {
  const std::uint32_t main_mode = (custom_mode >> kMainModeShift) & kByteMask;
  const std::uint32_t sub_mode = (custom_mode >> kSubModeShift) & kByteMask;
  const std::optional<std::string_view> main_name = find(kPx4MainModes, main_mode);
  if (!main_name) {
    return std::nullopt;
  }
  std::optional<std::string_view> sub_name;
  if (main_mode == kPx4Auto) {
    sub_name = find(kPx4AutoModes, sub_mode);
  } else if (main_mode == kPx4Posctl) {
    sub_name = find(kPx4PosctlModes, sub_mode);
  }
  return sub_name ? std::format("{}.{}", *main_name, *sub_name) : std::string(*main_name);
}

[[nodiscard]] std::optional<std::string> copter_mode(const Heartbeat& heartbeat) {
  if (std::ranges::find(kCopterTypes, heartbeat.type) == kCopterTypes.end()) {
    return std::nullopt;
  }
  const std::optional<std::string_view> name = find(kCopterModes, heartbeat.custom_mode);
  return name ? std::optional<std::string>(*name) : std::nullopt;
}

}  // namespace

std::string mode_name(const Heartbeat& heartbeat) {
  std::optional<std::string> name;
  if (heartbeat.autopilot == kAutopilotPx4) {
    name = px4_mode(heartbeat.custom_mode);
  } else if (heartbeat.autopilot == kAutopilotArduPilot) {
    name = copter_mode(heartbeat);
  }
  return name ? *name : std::format("custom mode {}", heartbeat.custom_mode);
}

}  // namespace ics::mavlink
