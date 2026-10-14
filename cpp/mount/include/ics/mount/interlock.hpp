#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <numbers>
#include <optional>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mount/keep_out.hpp"
#include "ics/mount/limits.hpp"
#include "ics/mount/pwi4_client.hpp"
#include "ics/mount/pwi4_status.hpp"
#include "ics/mount/sun.hpp"

namespace ics::mount {

// The station clock's health, as ics-timingd reports it (#150).
struct ClockHealth {
  // Locked to GNSS or in holdover: traceable to UTC.
  bool traceable = false;
  // The bound on the clock's error from UTC.
  Duration error_bound{};
  // When the report was made: a new report has a new time.
  UtcTime reported{};
};

// Where the interlock takes the time and the clock's health from: ics-mountd
// (ICS-041) feeds it from ics-timingd.
class ClockSource {
 public:
  ClockSource() = default;
  ClockSource(const ClockSource&) = delete;
  ClockSource& operator=(const ClockSource&) = delete;
  ClockSource(ClockSource&&) = delete;
  ClockSource& operator=(ClockSource&&) = delete;
  virtual ~ClockSource() = default;
  // UTC now.
  [[nodiscard]] virtual UtcTime now() = 0;
  // The latest report, or nothing before the first.
  [[nodiscard]] virtual std::optional<ClockHealth> health() = 0;
};

struct InterlockSettings {
  SunSettings sun;
  KeepOut keep_out;
  // PWI4's park position, where a fault sends the mount: it must be outside
  // the guard zone all year (sun_safe_all_year).
  AltAz park{};
  // A clock fault: an error bound over this, or no new report for this long.
  Duration max_error_bound = std::chrono::seconds(1);
  Duration max_report_age = std::chrono::seconds(30);
  // An ephemeris fault: PWI4's own distance to the sun differing from ours by
  // more than this.
  Radians max_disagreement{2.0 * std::numbers::pi / 180.0};
  // How often the sun's position is worked out again: it moves under 0.005°
  // a second.
  Duration sun_refresh = std::chrono::seconds(1);
};

// Why the interlock stopped the mount for good, until an operator clears it.
enum class SunFault : std::uint8_t {
  kNone,
  // The clock is free-running, its error bound is too large, or its reports
  // have stopped.
  kClock,
  // The sun's position could not be worked out, or PWI4's disagrees.
  kEphemeris,
  // The mount is inside the guard zone, and the limits leave it no way out.
  kBlocked,
};

// The interlock's state, which maps onto MountSample's sun fields (ICS-012).
struct InterlockState {
  // A good sun position is known, so commands may be sent.
  bool ready = false;
  // This period the interlock refused, stopped or moved the mount.
  bool active = false;
  // Moving the mount away from the sun.
  bool escaping = false;
  SunFault fault = SunFault::kNone;
  // A fault sent the mount to park, and its slew is watched.
  bool parking = false;
  // The last good sun position, and the line of sight's separation from it.
  AltAz sun{};
  Radians separation{};
  // Periods the interlock stopped a motion, refused rates or escaped.
  std::uint64_t stops = 0;
  std::uint64_t refused_rates = 0;
  std::uint64_t escapes = 0;
};

// One moment, on the steady clock and in UTC.
struct Instant {
  std::chrono::steady_clock::time_point steady{};
  UtcTime utc{};
};

// What the interlock does to the mount this period.
struct Action {
  enum class Kind : std::uint8_t {
    kNone,
    // Zero rates and /mount/stop.
    kStop,
    // Send rates.
    kEscape,
    // Zero rates, /mount/stop, then /mount/park.
    kPark,
  };
  Kind kind = Kind::kNone;
  AxisRates rates;
};

// Demanded rates after the interlock: zero when it pre-empted them.
struct GatedRates {
  AxisRates rates;
  bool preempted = false;
};

// The sun keep-out's decisions (ICS-034), apart from the mount, so that they
// can be tested in simulated time. Each period, supervise() takes the mount's
// status, the time and the clock's health, and says what to do; gate() and
// check_goto() then say which commands may be sent.
class SunGuard {
 public:
  // Error::kInvalidArgument when the park position is not outside the guard
  // zone all year at the site.
  [[nodiscard]] static Result<SunGuard> make(const frames::Geodetic& site, const InterlockSettings& settings,
                                             const MountLimits& limits);

  // One period. A fault latches: from then on it only watches a park slew,
  // stopping it if it heads into the guard zone around the last good sun,
  // grown by how far the sun can have moved since.
  [[nodiscard]] Action supervise(const MountStatus& status, const Instant& now,
                                 const std::optional<ClockHealth>& health);
  // Demanded rates, against the status supervise() last saw: zero unless
  // the interlock is ready, has no fault, is not escaping, and the motion
  // keeps clear of the guard zone (clear_ahead).
  [[nodiscard]] GatedRates gate(const AxisRates& rates, const MountStatus& status);
  // Error::kUnavailable unless ready, without a fault and not escaping;
  // Error::kOutOfRange for a target inside the guard zone.
  [[nodiscard]] Status check_goto(const AltAz& target) const;
  // An operator clears a fault; the interlock is ready again from the next
  // good period.
  void clear_fault() noexcept;

  [[nodiscard]] const InterlockState& state() const noexcept { return state_; }

 private:
  SunGuard(const frames::Geodetic& site, const InterlockSettings& settings, const MountLimits& limits) noexcept;

  enum class ClockVerdict : std::uint8_t { kGood, kWaiting, kFault };
  [[nodiscard]] ClockVerdict check_clock(const Instant& now, const std::optional<ClockHealth>& health);
  // Works the sun out again when due, and checks it against PWI4's.
  [[nodiscard]] SunFault update_sun(const MountStatus& status, const Instant& now);
  // Escapes, or stops a motion headed into the guard zone.
  [[nodiscard]] Action keep_out(const MountStatus& status, const Instant& now);
  [[nodiscard]] Action enter_fault(SunFault fault, const MountStatus& status, const Instant& now);
  [[nodiscard]] Action watch_park(const MountStatus& status, const Instant& now);
  // The guard zone around the last good sun, grown by its drift since.
  [[nodiscard]] KeepOut grown(const Instant& now) const noexcept;

  frames::Geodetic site_;
  InterlockSettings settings_;
  MountLimits limits_;
  InterlockState state_;
  bool started_ = false;
  bool have_sun_ = false;
  std::chrono::steady_clock::time_point last_report_steady_{};
  UtcTime last_reported_{};
  std::chrono::steady_clock::time_point sun_steady_{};
};

// Demanded rates as sent: what limit_rates made of the demand, with the
// rates sent, and whether the interlock pre-empted them.
struct SentRates {
  AppliedRates applied;
  bool preempted = false;
};

// The sun keep-out interlock around a PWI4 client (ICS-034): every command to
// the mount goes through it. supervise() runs each period, the rate loop's
// and ics-mountd's while idle; the commands are refused or zeroed as SunGuard
// decides. Not safe to use from two threads at once.
class SunInterlock {
 public:
  // clock must outlive the interlock. Error::kInvalidArgument as
  // SunGuard::make.
  [[nodiscard]] static Result<SunInterlock> make(Pwi4Client client, ClockSource& clock,
                                                 const frames::Geodetic& site, const InterlockSettings& settings);

  // The mount's status, connect and enable, as the client's: none moves the
  // mount.
  [[nodiscard]] Result<MountStatus> status(Deadline deadline);
  [[nodiscard]] Status connect(Deadline deadline);
  [[nodiscard]] Status enable(Axis axis, Deadline deadline);
  // One period against a status just read: decides, and sends what it
  // decided. Error::kUnavailable when a command it sent failed.
  [[nodiscard]] Result<Action::Kind> supervise(const MountStatus& status, Deadline deadline);
  // A goto, after check_goto: refused with its error, sending nothing.
  [[nodiscard]] Status goto_alt_az(const AltAz& target, Deadline deadline);
  // Demanded rates held to the limits, then through gate(), then sent.
  [[nodiscard]] Result<SentRates> set_rates(const AxisRates& demand, const MountStatus& status, Deadline deadline);
  [[nodiscard]] Status stop_rates(Deadline deadline);
  [[nodiscard]] Status stop(Deadline deadline);
  void clear_fault() noexcept { guard_.clear_fault(); }

  [[nodiscard]] const InterlockState& state() const noexcept { return guard_.state(); }
  [[nodiscard]] const ClientStats& client_stats() const noexcept { return client_.stats(); }

 private:
  SunInterlock(Pwi4Client client, ClockSource& clock, const SunGuard& guard) noexcept;

  Pwi4Client client_;
  std::reference_wrapper<ClockSource> clock_;
  SunGuard guard_;
};

}  // namespace ics::mount
