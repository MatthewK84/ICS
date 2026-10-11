#include "ics/mount/rate_loop.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>

#include "ics/common/check.hpp"

namespace ics::mount {
namespace {

using std::chrono::steady_clock;

// How long stopping the mount may take.
constexpr std::chrono::seconds kHaltTime{1};

}  // namespace

Status RateLoop::run(const std::atomic<bool>& stop) {
  if (!(settings_.rate_hz >= kMinLoopHz && settings_.rate_hz <= kMaxLoopHz)) {
    return fail(Error::kInvalidArgument);
  }
  const auto period = std::chrono::duration_cast<steady_clock::duration>(
      std::chrono::duration<double>(1.0 / settings_.rate_hz));
  steady_clock::time_point next = steady_clock::now();
  std::uint32_t failed_in_row = 0;
  while (!stop.load()) {
    const Deadline deadline = next + period;
    const Outcome outcome = step(deadline);
    ++stats_.periods;
    if (outcome == Outcome::kStale) {
      return halt(Fault::kStaleDemand);
    }
    failed_in_row = outcome == Outcome::kFailed ? failed_in_row + 1 : 0;
    stats_.failed_periods += outcome == Outcome::kFailed ? 1 : 0;
    if (failed_in_row >= settings_.max_failed_periods) {
      return halt(Fault::kLink);
    }
    const steady_clock::time_point now = steady_clock::now();
    stats_.late_periods += now > deadline ? 1 : 0;
    // A late period starts the next at once, rather than running two short.
    next = now > deadline ? now : deadline;
    std::this_thread::sleep_until(next);
  }
  static_cast<void>(client_.stop_rates(steady_clock::now() + kHaltTime));
  return {};
}

RateLoop::Outcome RateLoop::step(const Deadline deadline) {
  const std::optional<RateDemand> demand = source_.latest();
  if (!demand || settings_.clock() - demand->time > settings_.stale_after) {
    return Outcome::kStale;
  }
  const Result<MountStatus> status = client_.status(deadline);
  const Result<AppliedRates> applied =
      status ? client_.set_rates(demand->rates, *status, deadline) : fail(status.error());
  if (!applied) {
    return Outcome::kFailed;
  }
  for (std::size_t i = 0; i < kAxes; ++i) {
    stats_.clamped += applied->clamped[i] ? 1 : 0;
    stats_.stopped_at_limit += applied->stopped_at_limit[i] ? 1 : 0;
  }
  return Outcome::kSent;
}

Status RateLoop::halt(const Fault fault) {
  // A fault is reported once, by the period that found it.
  static_cast<void>(ics::check(stats_.fault == Fault::kNone));
  stats_.fault = fault;
  const Deadline deadline = steady_clock::now() + kHaltTime;
  static_cast<void>(client_.stop_rates(deadline));
  static_cast<void>(client_.stop(deadline));
  return fail(Error::kUnavailable);
}

}  // namespace ics::mount
