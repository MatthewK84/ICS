#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "ics/common/units.hpp"
#include "ics/mount/keep_out.hpp"
#include "ics/mount/limits.hpp"
#include "ics/mount/pwi4_status.hpp"

namespace ics::mount::testing {

// A simulated alt-az mount in simulated time, for the keep-out trials
// (ICS-034). Each axis turns at up to its top speed, which it reaches and
// sheds at its acceleration, and stops hard at its mechanical limits. A goto
// or a park slews both axes at once, each straight to its target, taking the
// azimuth either way round as told, since PWI4 does not say which way it
// goes; a rate command replaces a slew.
class SimMount {
 public:
  struct Axis {
    double max_rate = 0.0;
    double acceleration = 0.0;
    double min = 0.0;
    double max = 0.0;
  };

  SimMount(const std::array<Axis, kAxes>& axes, const AltAz& start) noexcept : axes_(axes) {
    position_[kAz] = start.azimuth.value();
    position_[kAlt] = start.altitude.value();
  }

  // Slews to target; long_way takes the azimuth the other way round when the
  // mechanical limits allow.
  void slew(const AltAz& target, const bool long_way) noexcept {
    const double here = position_[kAz];
    const double turn = 2.0 * std::numbers::pi;
    const double near = target.azimuth.value() + (turn * std::round((here - target.azimuth.value()) / turn));
    const double far = near + (near > here ? -turn : turn);
    const bool far_fits = far >= axes_[kAz].min && far <= axes_[kAz].max;
    goal_[kAz] = std::clamp(long_way && far_fits ? far : near, axes_[kAz].min, axes_[kAz].max);
    goal_[kAlt] = std::clamp(target.altitude.value(), axes_[kAlt].min, axes_[kAlt].max);
    slewing_ = true;
  }

  void set_rates(const AxisRates& rates) noexcept {
    slewing_ = false;
    for (std::size_t i = 0; i < kAxes; ++i) {
      rate_[i] = std::clamp(rates.rad_per_s[i], -axes_[i].max_rate, axes_[i].max_rate);
    }
  }

  void stop() noexcept { set_rates(AxisRates{}); }

  void step(const double dt) noexcept {
    for (std::size_t i = 0; i < kAxes; ++i) {
      const double wanted = slewing_ ? arrival_speed(i) : rate_[i];
      const double change = axes_[i].acceleration * dt;
      velocity_[i] = std::clamp(wanted, velocity_[i] - change, velocity_[i] + change);
      position_[i] += velocity_[i] * dt;
      if (position_[i] < axes_[i].min || position_[i] > axes_[i].max) {
        position_[i] = std::clamp(position_[i], axes_[i].min, axes_[i].max);
        velocity_[i] = 0.0;
      }
    }
  }

  [[nodiscard]] AltAz pointing() const noexcept {
    return {.altitude = Radians(position_[kAlt]), .azimuth = Radians(position_[kAz])};
  }

  // As PWI4 would report it, with the sun where it truly is.
  [[nodiscard]] MountStatus status(const AltAz& sun) const noexcept {
    MountStatus out;
    out.connected = true;
    out.slewing = slewing_;
    out.distance_to_sun = separation(pointing(), sun);
    for (std::size_t i = 0; i < kAxes; ++i) {
      out.axes[i] = {.enabled = true,
                     .position = Radians(position_[i]),
                     .velocity_rad_per_s = velocity_[i],
                     .min_position = Radians(axes_[i].min),
                     .max_position = Radians(axes_[i].max)};
    }
    return out;
  }

 private:
  static constexpr std::size_t kAz = 0;
  static constexpr std::size_t kAlt = 1;

  // The speed that brings a slewing axis to its goal and stops it there.
  [[nodiscard]] double arrival_speed(const std::size_t i) const noexcept {
    const double left = goal_[i] - position_[i];
    const double speed = std::min(axes_[i].max_rate, std::sqrt(2.0 * axes_[i].acceleration * std::abs(left)));
    return std::copysign(speed, left);
  }

  std::array<Axis, kAxes> axes_;
  std::array<double, kAxes> position_{};
  std::array<double, kAxes> velocity_{};
  std::array<double, kAxes> rate_{};
  std::array<double, kAxes> goal_{};
  bool slewing_ = false;
};

}  // namespace ics::mount::testing
