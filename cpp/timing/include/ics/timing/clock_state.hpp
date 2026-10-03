#pragma once

#include <cstdint>
#include <string_view>

#include "ics/timing/ptp_management.hpp"

namespace ics::timing {

// Whether this station's time is traceable to UTC through its grandmaster
// (ICS-019). The values match ics.v1.TimeQuality.ClockState.
enum class ClockState : std::uint8_t {
  kLocked = 1,       // The grandmaster is locked to GNSS, and this clock follows it.
  kHoldover = 2,     // The grandmaster lost GNSS and keeps time on its oscillator, within its specification.
  kFreeRunning = 3,  // Time is not traceable to UTC: no grandmaster, or one out of holdover.
};

// The state's name in logs: "locked", "holdover" or "free_running".
[[nodiscard]] std::string_view to_string(ClockState state) noexcept;

// The state the grandmaster's announced quality shows, as this clock's port
// sees it. A grandmaster that loses GNSS changes its clockClass (IEEE 1588
// 7.6.2.4; ITU-T G.8275.1 and G.8275.2):
//
//   6                   locked to a primary reference: locked, if it also says its time is traceable
//   7, 135, 140-160     in holdover within its specification: holdover
//   anything else       degraded, free-running, never locked or slave-only: free-running
//
// A port that is not SLAVE follows no grandmaster, so its time is free-running.
[[nodiscard]] ClockState classify(PortState port, const ClockQuality& quality,
                                  const TimePropertiesDataSet& time) noexcept;

}  // namespace ics::timing
