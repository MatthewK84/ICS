#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

#include "encoder_support.hpp"
#include "ics/encoder/pointing_model.hpp"

namespace ics::encoder::testing {

// One axis slewing back and forth (ICS-036's bench): it holds +rate for
// hold_s, decelerates at accel to -rate, holds that for hold_s, and
// accelerates back to +rate, over and over, from start_rad. The angle at
// time t is worked out in closed form, so the truth has no integration
// error.
class AxisMotion {
 public:
  AxisMotion(const double start_rad, const double rate, const double hold_s, const double accel)
      : start_(start_rad), rate_(rate), hold_(hold_s), accel_(accel), ramp_(2.0 * rate / accel) {}

  // The angle at t seconds, in radians.
  [[nodiscard]] double angle(const double t) const noexcept {
    const double phase = std::fmod(t, period());
    const double held = rate_ * hold_;
    if (phase < hold_) {
      return start_ + (rate_ * phase);
    }
    if (phase < hold_ + ramp_) {
      const double into = phase - hold_;
      return start_ + held + (rate_ * into) - (0.5 * accel_ * into * into);
    }
    if (phase < (2.0 * hold_) + ramp_) {
      return start_ + held - (rate_ * (phase - hold_ - ramp_));
    }
    const double into = phase - (2.0 * hold_) - ramp_;
    return start_ - (rate_ * into) + (0.5 * accel_ * into * into);
  }

  [[nodiscard]] double accel() const noexcept { return accel_; }

 private:
  [[nodiscard]] double period() const noexcept { return 2.0 * (hold_ + ramp_); }

  double start_;
  double rate_;
  double hold_;
  double accel_;
  double ramp_;
};

// The bench's mount: both axes at 0.1 rad/s, the azimuth turning through
// north and back so its single-turn encoder wraps, and the elevation encoder
// counting down as the axis goes up.
struct BenchMount {
  static constexpr double kRate = 0.1;
  AxisMotion azimuth{-0.15, kRate, 3.0, 1.0};
  AxisMotion elevation{0.7, kRate, 2.3, 0.5};

  [[nodiscard]] Direction truth(const double t) const noexcept {
    return Direction{.azimuth_rad = wrap_turn(azimuth.angle(t)), .elevation_rad = elevation.angle(t)};
  }

  // The azimuth encoder's counts: one turn, from 0 to 2^24 - 1.
  [[nodiscard]] std::int32_t azimuth_counts(const double t) const noexcept {
    const std::int64_t turn = kCountsPerRev;
    const std::int64_t counts = std::llround(azimuth.angle(t) / kCountRad);
    return static_cast<std::int32_t>(((counts % turn) + turn) % turn);
  }

  // The elevation encoder's counts, which fall as the axis rises.
  [[nodiscard]] std::int32_t elevation_counts(const double t) const noexcept { return -counts(elevation.angle(t)); }
};

}  // namespace ics::encoder::testing
