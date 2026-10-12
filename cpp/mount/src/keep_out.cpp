#include "ics/mount/keep_out.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>

#include "ics/common/check.hpp"

namespace ics::mount {
namespace {

constexpr auto kAz = static_cast<std::size_t>(Axis::kAzimuth);
constexpr auto kAlt = static_cast<std::size_t>(Axis::kAltitude);
constexpr double kTurn = 2.0 * std::numbers::pi;
// Separations closer than this are the same.
constexpr double kTolerance = 1e-9;
// How far ahead an escape is checked to move away.
constexpr double kEscapeCheckS = 0.1;
// Near the zenith an azimuth rate moves the line of sight little; an escape
// treats the altitude as at most 85° so its azimuth rate stays finite.
constexpr double kMinCosAltitude = 0.087;

[[nodiscard]] double wrap_turn(const double angle) noexcept { return angle - (kTurn * std::floor(angle / kTurn)); }

[[nodiscard]] std::array<double, 3> unit(const AltAz& d) noexcept {
  const double alt = d.altitude.value();
  const double az = d.azimuth.value();
  return {std::cos(alt) * std::cos(az), std::cos(alt) * std::sin(az), std::sin(alt)};
}

// The sweep's azimuth nearest the sun's.
[[nodiscard]] double nearest_azimuth(const Sweep& sweep, const double sun_azimuth) noexcept {
  const double low = sweep.azimuth_low.value();
  const double span = sweep.azimuth_high.value() - low;
  static_cast<void>(ics::check(span >= 0.0));
  const double past_low = wrap_turn(sun_azimuth - low);
  if (span >= kTurn || past_low <= span) {
    return sun_azimuth;
  }
  return (past_low - span) < (kTurn - past_low) ? sweep.azimuth_high.value() : low;
}

}  // namespace

Radians guard(const KeepOut& keep_out) noexcept { return Radians(keep_out.cone.value() + keep_out.margin.value()); }

AltAz pointing(const MountStatus& status) noexcept {
  return {.altitude = status.axes[kAlt].position, .azimuth = status.axes[kAz].position};
}

Radians separation(const AltAz& a, const AltAz& b) noexcept {
  const std::array<double, 3> u = unit(a);
  const std::array<double, 3> v = unit(b);
  const double cross_x = (u[1] * v[2]) - (u[2] * v[1]);
  const double cross_y = (u[2] * v[0]) - (u[0] * v[2]);
  const double cross_z = (u[0] * v[1]) - (u[1] * v[0]);
  const double dot = (u[0] * v[0]) + (u[1] * v[1]) + (u[2] * v[2]);
  return Radians(std::atan2(std::sqrt((cross_x * cross_x) + (cross_y * cross_y) + (cross_z * cross_z)), dot));
}

Radians closest_approach(const Sweep& sweep, const AltAz& sun) noexcept {
  // std::clamp needs low no higher than high.
  if (!ics::check(sweep.altitude_low.value() <= sweep.altitude_high.value())) {
    return Radians(0.0);
  }
  const double azimuth = nearest_azimuth(sweep, sun.azimuth.value());
  const double s = sun.altitude.value();
  // At that azimuth the separation shrinks as the altitude nears phi.
  const double phi = std::atan2(std::sin(s), std::cos(s) * std::cos(sun.azimuth.value() - azimuth));
  const double altitude = std::clamp(phi, sweep.altitude_low.value(), sweep.altitude_high.value());
  return separation({.altitude = Radians(altitude), .azimuth = Radians(azimuth)}, sun);
}

Sweep sweep_ahead(const MountStatus& status, const AxisRates& rates, const Duration lookahead) noexcept {
  const double seconds = std::chrono::duration<double>(lookahead).count();
  static_cast<void>(ics::check(seconds >= 0.0));
  std::array<double, kAxes> low{};
  std::array<double, kAxes> high{};
  for (std::size_t i = 0; i < kAxes; ++i) {
    const double position = status.axes[i].position.value();
    const double measured = status.axes[i].velocity_rad_per_s * seconds;
    const double commanded = rates.rad_per_s[i] * seconds;
    low[i] = position + std::min({0.0, measured, commanded});
    high[i] = position + std::max({0.0, measured, commanded});
  }
  return {.altitude_low = Radians(low[kAlt]),
          .altitude_high = Radians(high[kAlt]),
          .azimuth_low = Radians(low[kAz]),
          .azimuth_high = Radians(high[kAz])};
}

bool clear_ahead(const MountStatus& status, const AxisRates& rates, const AltAz& sun, const KeepOut& keep_out,
                 const Duration lookahead) noexcept {
  const double here = separation(pointing(status), sun).value();
  const double closest = closest_approach(sweep_ahead(status, rates, lookahead), sun).value();
  // The sweep holds where the mount is now.
  static_cast<void>(ics::check(closest <= here + kTolerance));
  return closest >= guard(keep_out).value() || closest >= here - kTolerance;
}

Status check_goto_sun(const AltAz& target, const AltAz& sun, const KeepOut& keep_out) noexcept {
  if (separation(target, sun).value() < guard(keep_out).value()) {
    return fail(Error::kOutOfRange);
  }
  return {};
}

std::optional<AxisRates> escape_rates(const MountStatus& status, const AltAz& sun, const MountLimits& limits,
                                      const KeepOut& keep_out) noexcept {
  static_cast<void>(ics::check(keep_out.escape_rate_rad_per_s > 0.0));
  const AltAz now = pointing(status);
  const double a = now.altitude.value();
  const double s = sun.altitude.value();
  const double delta = now.azimuth.value() - sun.azimuth.value();
  // Where the separation grows fastest on the sky: along the azimuth axis,
  // and upward. At the sun itself, any way is away; down is taken.
  const double across = std::cos(s) * std::sin(delta);
  const double up = (std::sin(a) * std::cos(s) * std::cos(delta)) - (std::cos(a) * std::sin(s));
  const double norm = std::hypot(across, up);
  const bool at_sun = norm < kTolerance;
  const double rate = keep_out.escape_rate_rad_per_s;
  AxisRates wanted;
  wanted.rad_per_s[kAz] = at_sun ? 0.0 : rate * across / (norm * std::max(std::cos(a), kMinCosAltitude));
  wanted.rad_per_s[kAlt] = at_sun ? -rate : rate * up / norm;
  const AxisRates allowed = limit_rates(wanted, status, limits).rates;
  const AltAz next{.altitude = Radians(a + (allowed.rad_per_s[kAlt] * kEscapeCheckS)),
                   .azimuth = Radians(now.azimuth.value() + (allowed.rad_per_s[kAz] * kEscapeCheckS))};
  if (separation(next, sun).value() <= separation(now, sun).value() + kTolerance) {
    return std::nullopt;
  }
  return allowed;
}

bool sun_safe_all_year(const AltAz& direction, const frames::Geodetic& site, const KeepOut& keep_out) noexcept {
  const double phi = to_radians(site.latitude()).value();
  const double h = direction.altitude.value();
  const double sin_dec =
      (std::sin(phi) * std::sin(h)) + (std::cos(phi) * std::cos(h) * std::cos(direction.azimuth.value()));
  static_cast<void>(ics::check(std::abs(sin_dec) <= 1.0 + kTolerance));
  const double declination = std::asin(std::clamp(sin_dec, -1.0, 1.0));
  return std::abs(declination) >= kMaxSunDeclination.value() + guard(keep_out).value();
}

}  // namespace ics::mount
