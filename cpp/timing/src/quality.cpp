#include "ics/timing/quality.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "ics/common/check.hpp"
#include "ics/timing/clock_state.hpp"

namespace ics::timing {
namespace {

// clockAccuracy 0x17 (1 ps) to 0x30 (10 s), in nanoseconds, rounded up.
constexpr std::uint8_t kFirstAccuracy = 0x17;
constexpr std::array<std::int64_t, 26> kAccuracyNs{
    1,          1,          1,          1,           1,           1,           1,          3,
    10,         25,         100,        250,         1'000,       2'500,       10'000,     25'000,
    100'000,    250'000,    1'000'000,  2'500'000,   10'000'000,  25'000'000,  100'000'000, 250'000'000,
    1'000'000'000, 10'000'000'000};

constexpr double kNanosecondsPerSecond = 1e9;

}  // namespace

Duration accuracy_bound(const std::uint8_t clock_accuracy) noexcept {
  if (clock_accuracy < kFirstAccuracy || clock_accuracy >= kFirstAccuracy + kAccuracyNs.size()) {
    return kUnbounded;
  }
  return Duration(kAccuracyNs[static_cast<std::size_t>(clock_accuracy - kFirstAccuracy)]);
}

QualityTracker::QualityTracker(const ErrorModel model) noexcept : model_(model) {}

bool QualityTracker::update(const Snapshot& snapshot, const SteadyTime now) noexcept {
  offset_ = snapshot.current.offset_from_master;
  path_delay_ = snapshot.current.mean_path_delay;
  accuracy_ = accuracy_bound(snapshot.parent.grandmaster_quality.clock_accuracy);
  return enter(classify(snapshot.port.port_state, snapshot.parent.grandmaster_quality, snapshot.time), now);
}

bool QualityTracker::lost(const SteadyTime now) noexcept {
  offset_ = Duration{};
  path_delay_ = Duration{};
  accuracy_ = kUnbounded;
  return enter(ClockState::kFreeRunning, now);
}

Quality QualityTracker::quality(const SteadyTime now) const noexcept {
  Quality quality{state_, Duration{}, offset_, path_delay_, kUnbounded};
  if (state_ == ClockState::kFreeRunning) {
    return quality;
  }
  if (state_ == ClockState::kHoldover) {
    // The steady clock never runs backwards, so neither does holdover.
    static_cast<void>(ics::check(now >= holdover_start_));
    quality.holdover = now - holdover_start_;
  }
  quality.error_bound = error_bound(quality.holdover);
  return quality;
}

bool QualityTracker::enter(const ClockState state, const SteadyTime now) noexcept {
  if (state == state_) {
    return false;
  }
  if (state == ClockState::kHoldover) {
    holdover_start_ = now;
  }
  state_ = state;
  return true;
}

Duration QualityTracker::error_bound(const Duration holdover) const noexcept {
  if (accuracy_ == kUnbounded) {
    return kUnbounded;
  }
  const double drift_ns = model_.holdover_drift_ns_per_s *
                          (static_cast<double>(holdover.count()) / kNanosecondsPerSecond);
  const double bound_ns = static_cast<double>(std::chrono::abs(offset_).count()) +
                          static_cast<double>(accuracy_.count()) +
                          static_cast<double>(model_.asymmetry_bound.count()) + drift_ns;
  // A bound past the largest Duration is no bound.
  if (!(bound_ns < static_cast<double>(kUnbounded.count()))) {
    return kUnbounded;
  }
  return Duration(static_cast<std::int64_t>(std::ceil(bound_ns)));
}

}  // namespace ics::timing
