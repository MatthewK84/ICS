#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ics::tagger {

// What the tagger's clock knows of UTC, as each sample carries it.
enum class ClockState : std::uint8_t {
  // Not yet locked: too few PPS edges in a row, or no UTC label yet. A
  // sample in this state has no time.
  kAcquiring = 0,
  // The last PPS edge came within the window of where the clock expected it.
  kLocked = 1,
  // A PPS edge is overdue: the time is extrapolated from the last edge at the
  // last rate, and its bound grows.
  kHoldover = 2,
};

// How the clock trusts its counter and the PPS edges.
struct ClockConfig {
  // The counter's nominal rate, in ticks per second.
  std::uint64_t nominal_hz = 0;
  // How far the counter's crystal may be from nominal, in parts per million;
  // it widens the window for the first second's edge.
  double max_offset_ppm = 50.0;
  // How far an edge may be from where the clock expects it, in ns.
  std::int64_t window_ns = 100'000;
  // The error bound while locked, in ns: the PPS edge's jitter, the counter's
  // resolution, and the rate's error over a second.
  std::int64_t locked_bound_ns = 1'000;
  // How fast the bound grows in holdover, in ns per second: the most the
  // crystal may wander from its last measured rate.
  std::int64_t holdover_drift_ns_per_s = 2'000;
};

// The PPS intervals the rate is averaged over.
inline constexpr std::size_t kRateHistory = 8;
// Accepted edges in a row that lock the clock, once it has a UTC label.
inline constexpr std::uint32_t kLockEdges = 3;
// Rejected edges in a row after which the clock starts again from the last:
// the PPS source has moved, not glitched.
inline constexpr std::uint32_t kMaxRejects = 3;

// What became of a PPS edge.
enum class PpsResult : std::uint8_t {
  kFirst,      // The first edge: the clock starts from it.
  kAccepted,   // An edge where the clock expected one, a whole number of seconds on.
  kRejected,   // An edge outside the window, taken as a glitch.
  kRestarted,  // The kMaxRejects-th rejected edge in a row: the clock starts again from it.
};

// What became of a UTC label.
enum class LabelResult : std::uint8_t {
  kApplied,    // The last edge's second, which the clock did not know.
  kConfirmed,  // The second the clock had counted to.
  kCorrected,  // A second other than the one counted to: the host's label wins.
  kRejected,   // No edge yet, or the label came more than half a second after it.
};

// A counter reading's UTC time, and how far it can be trusted.
struct Tag {
  // UTC, in nanoseconds since the Unix epoch; 0 while acquiring.
  std::int64_t utc_ns = 0;
  // The most the time may be off, in ns; 0 while acquiring.
  std::int64_t bound_ns = 0;
  ClockState state = ClockState::kAcquiring;
};

// UTC on the tagger's counter, disciplined by the GNSS PPS edge (ICS-032).
// Each edge marks the start of a UTC second; the station host labels which
// second, once, within half a second of an edge, and the clock counts on from
// there. The counter's rate is the mean of the last kRateHistory intervals
// between accepted edges, so a reading's time is its edge's second plus the
// ticks since the edge over that rate. Nothing allocates.
class PpsClock {
 public:
  // A config without a rate is taken as 1 Hz, after failing a check.
  explicit PpsClock(const ClockConfig& config) noexcept;

  // An edge captured at ticks of the 64-bit counter.
  [[nodiscard]] PpsResult on_pps(std::uint64_t ticks) noexcept;

  // The host's word that the last edge began UTC second utc_s, sent at
  // now_ticks.
  [[nodiscard]] LabelResult label(std::int64_t utc_s, std::uint64_t now_ticks) noexcept;

  // The UTC time of a counter reading, which may come shortly before the last
  // edge.
  [[nodiscard]] Tag tag(std::uint64_t ticks) const noexcept;

  // The counter's measured rate, in ticks per second.
  [[nodiscard]] double rate_hz() const noexcept { return rate_; }

 private:
  // Starts again from an edge at ticks, keeping the measured rate.
  void restart(std::uint64_t ticks) noexcept;
  // Counts in an edge a whole number of seconds after the last.
  void accept(std::uint64_t ticks, double interval, std::int64_t seconds) noexcept;
  // Counts a rejected edge, and starts again from it after kMaxRejects.
  [[nodiscard]] PpsResult reject(std::uint64_t ticks) noexcept;
  // How far from where the clock expects it an edge seconds on may be, in
  // ticks.
  [[nodiscard]] double tolerance(double seconds) const noexcept;
  [[nodiscard]] bool locked() const noexcept { return streak_ >= kLockEdges && labeled_; }

  ClockConfig config_;
  double rate_;
  std::array<double, kRateHistory> intervals_{};
  std::size_t interval_count_ = 0;
  std::size_t next_interval_ = 0;
  bool have_edge_ = false;
  std::uint64_t edge_ = 0;
  std::uint32_t streak_ = 0;
  std::uint32_t rejects_ = 0;
  bool labeled_ = false;
  std::int64_t edge_utc_s_ = 0;
};

}  // namespace ics::tagger
