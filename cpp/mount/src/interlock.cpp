#include "ics/mount/interlock.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

#include "ics/common/check.hpp"

namespace ics::mount {
namespace {

using std::chrono::steady_clock;

constexpr double kTurn = 2.0 * std::numbers::pi;
// The fastest the sun moves across the sky against a fixed direction: 15° an
// hour.
constexpr double kSunDriftRadPerS = (15.0 / 3600.0) * std::numbers::pi / 180.0;

// The sweep a straight move from here to there covers, taking the shorter
// way round in azimuth.
[[nodiscard]] Sweep sweep_between(const AltAz& here, const AltAz& there) noexcept {
  const double az_here = here.azimuth.value();
  const double az_there = there.azimuth.value() + (kTurn * std::round((az_here - there.azimuth.value()) / kTurn));
  // The shorter way is at most half a turn.
  static_cast<void>(ics::check(std::abs(az_there - az_here) <= (kTurn / 2.0) + 1e-9));
  return {.altitude_low = Radians(std::min(here.altitude.value(), there.altitude.value())),
          .altitude_high = Radians(std::max(here.altitude.value(), there.altitude.value())),
          .azimuth_low = Radians(std::min(az_here, az_there)),
          .azimuth_high = Radians(std::max(az_here, az_there))};
}

}  // namespace

Result<SunGuard> SunGuard::make(const frames::Geodetic& site, const InterlockSettings& settings,
                                const MountLimits& limits) {
  if (!sun_safe_all_year(settings.park, site, settings.keep_out)) {
    return fail(Error::kInvalidArgument);
  }
  return SunGuard(site, settings, limits);
}

SunGuard::SunGuard(const frames::Geodetic& site, const InterlockSettings& settings, const MountLimits& limits) noexcept
    : site_(site), settings_(settings), limits_(limits) {}

Action SunGuard::supervise(const MountStatus& status, const Instant& now, const std::optional<ClockHealth>& health) {
  state_.active = false;
  if (!started_) {
    started_ = true;
    last_report_steady_ = now.steady;
  }
  if (state_.fault != SunFault::kNone) {
    return watch_park(status, now);
  }
  const ClockVerdict clock = check_clock(now, health);
  if (clock == ClockVerdict::kFault) {
    return enter_fault(SunFault::kClock, status, now);
  }
  if (clock == ClockVerdict::kWaiting) {
    state_.ready = false;
    return {};
  }
  const SunFault ephemeris = update_sun(status, now);
  if (ephemeris != SunFault::kNone) {
    return enter_fault(ephemeris, status, now);
  }
  state_.ready = true;
  return keep_out(status, now);
}

SunGuard::ClockVerdict SunGuard::check_clock(const Instant& now, const std::optional<ClockHealth>& health) {
  if (health && health->reported != last_reported_) {
    last_reported_ = health->reported;
    last_report_steady_ = now.steady;
  }
  if (now.steady - last_report_steady_ > settings_.max_report_age) {
    return ClockVerdict::kFault;
  }
  if (!health) {
    return ClockVerdict::kWaiting;
  }
  if (!health->traceable || health->error_bound > settings_.max_error_bound) {
    return ClockVerdict::kFault;
  }
  return ClockVerdict::kGood;
}

SunFault SunGuard::update_sun(const MountStatus& status, const Instant& now) {
  const bool due = !have_sun_ || now.steady - sun_steady_ >= settings_.sun_refresh;
  AltAz sun = state_.sun;
  if (due) {
    const Result<AltAz> worked_out = sun_position(now.utc, site_, settings_.sun);
    if (!worked_out || !std::isfinite(worked_out->altitude.value() + worked_out->azimuth.value())) {
      return SunFault::kEphemeris;
    }
    sun = *worked_out;
  }
  const Radians apart = separation(pointing(status), sun);
  // PWI4 reports 0 while it is not connected to the mount.
  if (status.connected &&
      std::abs(status.distance_to_sun.value() - apart.value()) > settings_.max_disagreement.value()) {
    return SunFault::kEphemeris;
  }
  sun_steady_ = due ? now.steady : sun_steady_;
  have_sun_ = true;
  state_.sun = sun;
  state_.separation = apart;
  return SunFault::kNone;
}

Action SunGuard::keep_out(const MountStatus& status, const Instant& now) {
  static_cast<void>(ics::check(state_.ready));
  const double zone = guard(settings_.keep_out).value();
  const double apart = state_.separation.value();
  if (state_.escaping && apart >= zone + settings_.keep_out.escape_clearance.value()) {
    state_.escaping = false;
    state_.active = true;
    return {.kind = Action::Kind::kStop, .rates = {}};
  }
  if (state_.escaping || apart < zone) {
    const std::optional<AxisRates> away = escape_rates(status, state_.sun, limits_, settings_.keep_out);
    if (!away) {
      return enter_fault(SunFault::kBlocked, status, now);
    }
    state_.escaping = true;
    state_.active = true;
    ++state_.escapes;
    return {.kind = Action::Kind::kEscape, .rates = *away};
  }
  if (!clear_ahead(status, AxisRates{}, state_.sun, settings_.keep_out, limits_.lookahead)) {
    state_.active = true;
    ++state_.stops;
    return {.kind = Action::Kind::kStop, .rates = {}};
  }
  return {};
}

Action SunGuard::enter_fault(const SunFault fault, const MountStatus& status, const Instant& now) {
  static_cast<void>(ics::check(fault != SunFault::kNone));
  state_.fault = fault;
  state_.ready = false;
  state_.escaping = false;
  state_.active = true;
  ++state_.stops;
  // With no good sun ever known, nothing says which way is safe: hold.
  if (!have_sun_) {
    return {.kind = Action::Kind::kStop, .rates = {}};
  }
  const Sweep to_park = sweep_between(pointing(status), settings_.park);
  if (closest_approach(to_park, state_.sun).value() < guard(grown(now)).value()) {
    return {.kind = Action::Kind::kStop, .rates = {}};
  }
  state_.parking = true;
  return {.kind = Action::Kind::kPark, .rates = {}};
}

Action SunGuard::watch_park(const MountStatus& status, const Instant& now) {
  static_cast<void>(ics::check(state_.fault != SunFault::kNone));
  if (!state_.parking || clear_ahead(status, AxisRates{}, state_.sun, grown(now), limits_.lookahead)) {
    return {};
  }
  state_.parking = false;
  state_.active = true;
  ++state_.stops;
  return {.kind = Action::Kind::kStop, .rates = {}};
}

KeepOut SunGuard::grown(const Instant& now) const noexcept {
  KeepOut out = settings_.keep_out;
  const double since = std::chrono::duration<double>(now.steady - sun_steady_).count();
  out.margin = Radians(out.margin.value() + (kSunDriftRadPerS * std::max(since, 0.0)));
  static_cast<void>(ics::check(out.margin.value() >= settings_.keep_out.margin.value()));
  return out;
}

GatedRates SunGuard::gate(const AxisRates& rates, const MountStatus& status) {
  // Rates come held to the limits, which take a demand that is not a number
  // as 0.
  static_cast<void>(ics::check(std::isfinite(rates.rad_per_s[0] + rates.rad_per_s[1])));
  if (state_.ready && !state_.escaping &&
      clear_ahead(status, rates, state_.sun, settings_.keep_out, limits_.lookahead)) {
    return {.rates = rates, .preempted = false};
  }
  state_.active = true;
  ++state_.refused_rates;
  return {.rates = {}, .preempted = true};
}

Status SunGuard::check_goto(const AltAz& target) const {
  if (!state_.ready || state_.escaping) {
    return fail(Error::kUnavailable);
  }
  return check_goto_sun(target, state_.sun, settings_.keep_out);
}

void SunGuard::clear_fault() noexcept {
  state_.fault = SunFault::kNone;
  state_.parking = false;
  have_sun_ = false;
}

Result<SunInterlock> SunInterlock::make(Pwi4Client client, ClockSource& clock, const frames::Geodetic& site,
                                        const InterlockSettings& settings) {
  Result<SunGuard> guard = SunGuard::make(site, settings, client.settings().limits);
  if (!guard) {
    return fail(guard.error());
  }
  return SunInterlock(std::move(client), clock, *guard);
}

SunInterlock::SunInterlock(Pwi4Client client, ClockSource& clock, const SunGuard& guard) noexcept
    : client_(std::move(client)), clock_(clock), guard_(guard) {}

Result<MountStatus> SunInterlock::status(const Deadline deadline) { return client_.status(deadline); }

Status SunInterlock::connect(const Deadline deadline) { return client_.connect(deadline); }

Status SunInterlock::enable(const Axis axis, const Deadline deadline) { return client_.enable(axis, deadline); }

Result<Action::Kind> SunInterlock::supervise(const MountStatus& status, const Deadline deadline) {
  const Instant now{.steady = steady_clock::now(), .utc = clock_.get().now()};
  const Action action = guard_.supervise(status, now, clock_.get().health());
  if (action.kind == Action::Kind::kNone) {
    return action.kind;
  }
  if (action.kind == Action::Kind::kEscape) {
    const Result<AppliedRates> sent = client_.set_rates(action.rates, status, deadline);
    return sent ? Result<Action::Kind>(action.kind) : fail(sent.error());
  }
  // Each is sent whether or not the one before it reached the mount.
  const Status zero = client_.stop_rates(deadline);
  const Status halted = client_.stop(deadline);
  const Status parked = action.kind == Action::Kind::kPark ? client_.park(deadline) : Status{};
  const std::array<bool, 3> sent{zero.has_value(), halted.has_value(), parked.has_value()};
  if (std::ranges::find(sent, false) != sent.end()) {
    return fail(Error::kUnavailable);
  }
  return action.kind;
}

Status SunInterlock::goto_alt_az(const AltAz& target, const Deadline deadline) {
  const Status allowed = guard_.check_goto(target);
  if (!allowed) {
    return allowed;
  }
  return client_.goto_alt_az(target, deadline);
}

Result<SentRates> SunInterlock::set_rates(const AxisRates& demand, const MountStatus& status,
                                          const Deadline deadline) {
  // The interlock judges the rates the limits leave, which are what is sent.
  AppliedRates limited = limit_rates(demand, status, client_.settings().limits);
  const GatedRates gated = guard_.gate(limited.rates, status);
  const Result<AppliedRates> sent = client_.set_rates(gated.rates, status, deadline);
  if (!sent) {
    return fail(sent.error());
  }
  limited.rates = sent->rates;
  return SentRates{.applied = limited, .preempted = gated.preempted};
}

Status SunInterlock::stop_rates(const Deadline deadline) { return client_.stop_rates(deadline); }

Status SunInterlock::stop(const Deadline deadline) { return client_.stop(deadline); }

}  // namespace ics::mount
