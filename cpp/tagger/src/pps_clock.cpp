#include "ics/tagger/pps_clock.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "ics/common/check.hpp"

namespace ics::tagger {
namespace {

constexpr double kNsPerSecond = 1e9;
constexpr double kPerMillion = 1e-6;

[[nodiscard]] double nominal_rate(const ClockConfig& config) noexcept {
  return ics::check(config.nominal_hz > 0) ? static_cast<double>(config.nominal_hz) : 1.0;
}

}  // namespace

PpsClock::PpsClock(const ClockConfig& config) noexcept : config_(config), rate_(nominal_rate(config)) {}

PpsResult PpsClock::on_pps(const std::uint64_t ticks) noexcept {
  if (!have_edge_) {
    restart(ticks);
    return PpsResult::kFirst;
  }
  if (ticks <= edge_) {
    return reject(ticks);
  }
  const auto elapsed = static_cast<double>(ticks - edge_);
  const double seconds = std::round(elapsed / rate_);
  if (seconds < 1.0 || std::abs(elapsed - (seconds * rate_)) > tolerance(seconds)) {
    return reject(ticks);
  }
  accept(ticks, elapsed / seconds, static_cast<std::int64_t>(seconds));
  return PpsResult::kAccepted;
}

LabelResult PpsClock::label(const std::int64_t utc_s, const std::uint64_t now_ticks) noexcept {
  const bool recent = have_edge_ && now_ticks >= edge_ && static_cast<double>(now_ticks - edge_) <= rate_ / 2.0;
  if (!recent) {
    return LabelResult::kRejected;
  }
  if (!labeled_) {
    labeled_ = true;
    edge_utc_s_ = utc_s;
    return LabelResult::kApplied;
  }
  if (utc_s == edge_utc_s_) {
    return LabelResult::kConfirmed;
  }
  edge_utc_s_ = utc_s;
  return LabelResult::kCorrected;
}

Tag PpsClock::tag(const std::uint64_t ticks) const noexcept {
  if (!locked()) {
    return {};
  }
  // A reading latched just before the edge it is stamped after is negative.
  const auto elapsed = static_cast<double>(static_cast<std::int64_t>(ticks - edge_));
  const double seconds = elapsed / rate_;
  Tag out{.utc_ns = (edge_utc_s_ * static_cast<std::int64_t>(kNsPerSecond)) + std::llround(seconds * kNsPerSecond),
          .bound_ns = config_.locked_bound_ns,
          .state = ClockState::kLocked};
  const double overdue = seconds - 1.0;
  if (overdue * kNsPerSecond > static_cast<double>(config_.window_ns)) {
    out.state = ClockState::kHoldover;
    out.bound_ns += std::llround(overdue * static_cast<double>(config_.holdover_drift_ns_per_s));
  }
  return out;
}

void PpsClock::restart(const std::uint64_t ticks) noexcept {
  have_edge_ = true;
  edge_ = ticks;
  streak_ = 1;
  rejects_ = 0;
  labeled_ = false;
}

void PpsClock::accept(const std::uint64_t ticks, const double interval, const std::int64_t seconds) noexcept {
  // on_pps accepts only edges a whole number of seconds on.
  static_cast<void>(ics::check(seconds >= 1));
  static_cast<void>(ics::check(interval > 0.0));
  intervals_[next_interval_] = interval;
  next_interval_ = (next_interval_ + 1) % kRateHistory;
  interval_count_ = std::min(interval_count_ + 1, kRateHistory);
  double sum = 0.0;
  for (std::size_t i = 0; i < interval_count_; ++i) {
    sum += intervals_[i];
  }
  rate_ = sum / static_cast<double>(interval_count_);
  edge_ = ticks;
  streak_ = std::min(streak_ + 1, kLockEdges);
  rejects_ = 0;
  if (labeled_) {
    edge_utc_s_ += seconds;
  }
}

PpsResult PpsClock::reject(const std::uint64_t ticks) noexcept {
  ++rejects_;
  if (rejects_ < kMaxRejects) {
    return PpsResult::kRejected;
  }
  restart(ticks);
  return PpsResult::kRestarted;
}

double PpsClock::tolerance(const double seconds) const noexcept {
  // Without a measured rate, the crystal may be anywhere in its tolerance;
  // with one, it may have wandered since.
  const double drift_per_s = interval_count_ == 0
                                 ? config_.max_offset_ppm * kPerMillion
                                 : static_cast<double>(config_.holdover_drift_ns_per_s) / kNsPerSecond;
  const double window_s = static_cast<double>(config_.window_ns) / kNsPerSecond;
  return (window_s + (drift_per_s * seconds)) * rate_;
}

}  // namespace ics::tagger
