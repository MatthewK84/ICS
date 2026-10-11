#include "ics/mount/limits.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>

#include "ics/common/check.hpp"

namespace ics::mount {
namespace {

// The narrower of an axis's own limits and the mount's.
struct Range {
  double min = 0.0;
  double max = 0.0;
};

[[nodiscard]] Range range(const AxisLimits& limits, const AxisStatus& axis) noexcept {
  return {.min = std::max(limits.min.value(), axis.min_position.value()),
          .max = std::min(limits.max.value(), axis.max_position.value())};
}

}  // namespace

Status check_goto(const AltAz& target, const MountStatus& status, const MountLimits& limits) noexcept {
  const auto altitude = static_cast<std::size_t>(Axis::kAltitude);
  const Range allowed = range(limits.axes[altitude], status.axes[altitude]);
  if (target.altitude.value() < allowed.min || target.altitude.value() > allowed.max) {
    return fail(Error::kOutOfRange);
  }
  return {};
}

AppliedRates limit_rates(const AxisRates& demand, const MountStatus& status, const MountLimits& limits) noexcept {
  const double lookahead_s = std::chrono::duration<double>(limits.lookahead).count();
  AppliedRates out;
  for (std::size_t i = 0; i < kAxes; ++i) {
    // A maximum rate is not negative; one that is stops the axis.
    static_cast<void>(ics::check(limits.axes[i].max_rate_rad_per_s >= 0.0));
    const double max_rate = std::max(limits.axes[i].max_rate_rad_per_s, 0.0);
    // A demand that is not a number is taken as 0, and counts as clamped.
    const double wanted = std::isfinite(demand.rad_per_s[i]) ? demand.rad_per_s[i] : 0.0;
    const double rate = std::clamp(wanted, -max_rate, max_rate);
    out.clamped[i] = rate != demand.rad_per_s[i];
    const Range allowed = range(limits.axes[i], status.axes[i]);
    const double ahead = status.axes[i].position.value() + (rate * lookahead_s);
    out.stopped_at_limit[i] = (rate > 0.0 && ahead > allowed.max) || (rate < 0.0 && ahead < allowed.min);
    out.rates.rad_per_s[i] = out.stopped_at_limit[i] ? 0.0 : rate;
  }
  return out;
}

}  // namespace ics::mount
