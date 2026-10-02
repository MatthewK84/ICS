#include "ics/timing/clock_state.hpp"

#include <cstdint>
#include <string_view>

#include "ics/timing/ptp_management.hpp"

namespace ics::timing {
namespace {

constexpr std::uint8_t kPrimaryReference = 6;
constexpr std::uint8_t kHoldoverInSpec = 7;
// ITU-T G.8275.1 and G.8275.2: a boundary clock in holdover within
// specification, and the grandmaster holdover categories 1 to 3.
constexpr std::uint8_t kBoundaryHoldover = 135;
constexpr std::uint8_t kFirstHoldoverCategory = 140;
constexpr std::uint8_t kLastHoldoverCategory = 160;

[[nodiscard]] bool in_holdover(const std::uint8_t clock_class) noexcept {
  return clock_class == kHoldoverInSpec || clock_class == kBoundaryHoldover ||
         (clock_class >= kFirstHoldoverCategory && clock_class <= kLastHoldoverCategory);
}

}  // namespace

std::string_view to_string(const ClockState state) noexcept {
  switch (state) {
    case ClockState::kLocked:
      return "locked";
    case ClockState::kHoldover:
      return "holdover";
    case ClockState::kFreeRunning:
      return "free_running";
  }
  return "unknown";
}

ClockState classify(const PortState port, const ClockQuality& quality, const TimePropertiesDataSet& time) noexcept {
  if (port != PortState::kSlave) {
    return ClockState::kFreeRunning;
  }
  if (quality.clock_class == kPrimaryReference && time.time_traceable()) {
    return ClockState::kLocked;
  }
  if (in_holdover(quality.clock_class)) {
    return ClockState::kHoldover;
  }
  return ClockState::kFreeRunning;
}

}  // namespace ics::timing
