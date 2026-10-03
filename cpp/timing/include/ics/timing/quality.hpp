#pragma once

#include <chrono>
#include <cstdint>

#include "ics/common/units.hpp"
#include "ics/timing/clock_state.hpp"
#include "ics/timing/ptp_management.hpp"

namespace ics::timing {

// What one poll of ptp4l read (ICS-019).
struct Snapshot {
  PortDataSet port;
  CurrentDataSet current;
  ParentDataSet parent;
  TimePropertiesDataSet time;
};

// Monotonic time, for timing holdover.
using SteadyTime = std::chrono::steady_clock::time_point;

// The settings of the error bound.
struct ErrorModel {
  // The largest path asymmetry between this host and the grandmaster. PTP
  // cannot measure it, so it adds to every offset's error.
  Duration asymmetry_bound{};
  // How fast the grandmaster's oscillator can drift from UTC in holdover, in
  // nanoseconds per second.
  double holdover_drift_ns_per_s = 0.0;
};

// The error bound when this station's time is not traceable to UTC.
inline constexpr Duration kUnbounded = Duration::max();

// The timing service's view of this station's time: the fields of
// ics.v1.TimeQuality it fills.
struct Quality {
  ClockState state = ClockState::kFreeRunning;
  // How long the grandmaster has been in holdover, as first seen here; zero
  // unless the state is holdover.
  Duration holdover{};
  Duration ptp_offset{};
  Duration ptp_path_delay{};
  Duration error_bound = kUnbounded;
};

// The bound a grandmaster's clockAccuracy announces (IEEE 1588-2019 Table 5),
// rounded up to whole nanoseconds; kUnbounded for "unknown" (0xFE), "worse than
// 10 s" and values the standard reserves.
[[nodiscard]] Duration accuracy_bound(std::uint8_t clock_accuracy) noexcept;

// The clock state across polls, with the time spent in holdover. The error
// bound is a model, not a measurement:
//
//   locked         |offset| + the grandmaster's accuracy + the asymmetry bound
//   holdover       the same, plus the drift rate times the holdover so far
//   free-running   unbounded
class QualityTracker {
 public:
  explicit QualityTracker(ErrorModel model) noexcept;

  // One poll's data sets, read at now. True when the state changed.
  bool update(const Snapshot& snapshot, SteadyTime now) noexcept;
  // ptp4l did not answer the poll at now. True when the state changed.
  bool lost(SteadyTime now) noexcept;

  [[nodiscard]] ClockState state() const noexcept { return state_; }
  [[nodiscard]] Quality quality(SteadyTime now) const noexcept;

 private:
  bool enter(ClockState state, SteadyTime now) noexcept;
  [[nodiscard]] Duration error_bound(Duration holdover) const noexcept;

  ErrorModel model_;
  ClockState state_ = ClockState::kFreeRunning;
  SteadyTime holdover_start_{};
  Duration offset_{};
  Duration path_delay_{};
  Duration accuracy_ = kUnbounded;
};

}  // namespace ics::timing
