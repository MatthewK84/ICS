#pragma once

#include <array>
#include <chrono>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/mount/pwi4_status.hpp"

namespace ics::mount {

// One axis's limits, in the axis's own coordinates, as PWI4 reports its
// position. The mount's own mechanical limits apply as well; the narrower of
// the two wins.
struct AxisLimits {
  Radians min{};
  Radians max{};
  // The fastest the axis may be commanded to turn.
  double max_rate_rad_per_s = 0.0;
};

// The limits ICS holds a station mount to (ICS-033).
struct MountLimits {
  // By Axis.
  std::array<AxisLimits, kAxes> axes{};
  // How far ahead a rate is projected: an axis whose rate would carry it
  // past a limit within this time is stopped.
  Duration lookahead = std::chrono::seconds(1);
};

// Both axes' rates, in radians per second.
struct AxisRates {
  // By Axis.
  std::array<double, kAxes> rad_per_s{};
};

// The rates sent, and why they differ from the demand: by Axis, whether its
// rate was clamped to its maximum, and whether it was stopped short of a
// limit.
struct AppliedRates {
  AxisRates rates;
  std::array<bool, kAxes> clamped{};
  std::array<bool, kAxes> stopped_at_limit{};
};

// Where a goto points.
struct AltAz {
  Radians altitude{};
  Radians azimuth{};
};

// Whether a goto may be sent: Error::kOutOfRange for an altitude outside the
// altitude axis's limits, its own or the mount's. A goto's azimuth is taken
// from 0 to 2π and the mount chooses how to reach it through its cable wrap,
// enforcing its own azimuth limits, so azimuth is not checked here.
[[nodiscard]] Status check_goto(const AltAz& target, const MountStatus& status, const MountLimits& limits) noexcept;

// The rates to send for a demand: each clamped to its axis's maximum, then
// set to 0 if it would carry its axis past a limit within the lookahead. A
// rate away from a limit is never stopped, so an axis past one can always
// come back.
[[nodiscard]] AppliedRates limit_rates(const AxisRates& demand, const MountStatus& status,
                                       const MountLimits& limits) noexcept;

}  // namespace ics::mount
