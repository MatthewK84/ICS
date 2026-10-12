#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/logging/logger.hpp"
#include "ics/mount/limits.hpp"
#include "ics/mount/pwi4_client.hpp"

namespace ics::mount {

// A rate demand, and when it was made.
struct RateDemand {
  AxisRates rates;
  UtcTime time{};
};

// Where the rate loop takes its demands from: the cue extrapolator and the
// tracker (ICS-037, ICS-038).
class RateSource {
 public:
  RateSource() = default;
  RateSource(const RateSource&) = delete;
  RateSource& operator=(const RateSource&) = delete;
  RateSource(RateSource&&) = delete;
  RateSource& operator=(RateSource&&) = delete;
  virtual ~RateSource() = default;

  // The latest demand, or nothing before the first.
  [[nodiscard]] virtual std::optional<RateDemand> latest() = 0;
};

// The slowest and fastest loops (ICS-033).
inline constexpr double kMinLoopHz = 20.0;
inline constexpr double kMaxLoopHz = 50.0;

struct LoopSettings {
  // Periods a second, kMinLoopHz to kMaxLoopHz.
  double rate_hz = kMaxLoopHz;
  // A demand older than this is stale, which stops the mount.
  Duration stale_after = std::chrono::milliseconds(200);
  // Failed periods in a row that stop the mount.
  std::uint32_t max_failed_periods = 3;
  // UTC, to age the demands by.
  logging::Clock clock = &logging::system_now;
};

// Why the loop stopped the mount.
enum class Fault : std::uint8_t {
  kNone,
  // max_failed_periods periods in a row did not reach the mount.
  kLink,
  // No demand, or none fresher than stale_after.
  kStaleDemand,
};

struct LoopStats {
  std::uint64_t periods = 0;
  // Periods whose work ran past the period's end.
  std::uint64_t late_periods = 0;
  // Periods whose status or rates did not reach the mount.
  std::uint64_t failed_periods = 0;
  // Axis rates clamped to their maximum, and stopped short of a limit.
  std::uint64_t clamped = 0;
  std::uint64_t stopped_at_limit = 0;
  Fault fault = Fault::kNone;
};

// Sends the latest rate demand to the mount at a fixed rate (ICS-033). Each
// period it asks for the mount's status and sends the demand, held to the
// limits against that status, all within the period. On a fault it sends
// zero rates and /mount/stop, and returns; it never starts the mount moving
// again on its own.
class RateLoop {
 public:
  // client and source must outlive the loop.
  RateLoop(Pwi4Client& client, RateSource& source, const LoopSettings& settings) noexcept
      : client_(client), source_(source), settings_(settings) {}

  // Runs until stop is set, then sends zero rates. Error::kInvalidArgument,
  // running nothing, for a rate outside kMinLoopHz to kMaxLoopHz;
  // Error::kUnavailable after a fault, which stats() names.
  [[nodiscard]] Status run(const std::atomic<bool>& stop);

  [[nodiscard]] const LoopStats& stats() const noexcept { return stats_; }

 private:
  enum class Outcome : std::uint8_t { kSent, kFailed, kStale };

  // One period's work, done by deadline.
  [[nodiscard]] Outcome step(Deadline deadline);
  // Stops the mount for a fault, and returns kUnavailable.
  [[nodiscard]] Status halt(Fault fault);

  Pwi4Client& client_;
  RateSource& source_;
  LoopSettings settings_;
  LoopStats stats_;
};

}  // namespace ics::mount
