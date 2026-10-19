// estimator.cpp: the alpha-beta track of the target's direction (ICS-038).
// JSF deviation D-6: ics::Result, ics::check, ics::UtcTime and ics::encoder come from .hpp headers.
#include <optional>

#include <ics/common/check.hpp>
#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/tracker/estimator.h>
#include <ics/tracker/geometry.h>

namespace
{

using ics::float64;

constexpr float64 nanoseconds_per_second = 1.0E9;
constexpr float64 max_beta_plus_two_alpha = 4.0;

// The seconds from one time to another.
float64 seconds_between(const ics::UtcTime from, const ics::UtcTime to)
{
  return static_cast<float64>((to - from).count()) / nanoseconds_per_second;
}

}  // namespace

namespace ics::tracker
{

bool stable(const Estimator_settings& settings)
{
  const bool alpha_in = (settings.alpha > 0.0) && (settings.alpha <= 1.0);
  return alpha_in && (settings.beta >= 0.0) && (settings.beta < (max_beta_plus_two_alpha - (2.0 * settings.alpha)));
}

Angle_state carried(const Angle_state& state, const UtcTime time)
{
  const float64 seconds = seconds_between(state.time, time);
  return {time,
          wrap_azimuth(state.azimuth_rad + (state.azimuth_rate_rad_s * seconds)),
          state.elevation_rad + (state.elevation_rate_rad_s * seconds),
          state.azimuth_rate_rad_s,
          state.elevation_rate_rad_s};
}

Angle_estimator::Angle_estimator(const Estimator_settings& settings) : settings_(settings)
{
  static_cast<void>(check(stable(settings)));
}

void Angle_estimator::start(const Angle_state& state)
{
  state_ = state;
}

Result<Angle_state> Angle_estimator::state() const
{
  Result<Angle_state> current = fail(Error::kEmpty);
  if (state_.has_value())
  {
    current = *state_;
  }
  return current;
}

Result<Angle_state> Angle_estimator::update(const UtcTime time, const encoder::Direction& measured)
{
  Result<Angle_state> updated = state();
  if (updated.has_value() && (time <= updated->time))
  {
    updated = fail(Error::kOutOfRange);
  }
  if (updated.has_value())
  {
    const float64 seconds = seconds_between(updated->time, time);
    const Angle_state predicted = carried(*updated, time);
    const float64 azimuth_residual = encoder::wrap_pi(measured.azimuth_rad - predicted.azimuth_rad);
    const float64 elevation_residual = measured.elevation_rad - predicted.elevation_rad;
    state_ = Angle_state{time,
                         wrap_azimuth(predicted.azimuth_rad + (settings_.alpha * azimuth_residual)),
                         predicted.elevation_rad + (settings_.alpha * elevation_residual),
                         predicted.azimuth_rate_rad_s + ((settings_.beta * azimuth_residual) / seconds),
                         predicted.elevation_rate_rad_s + ((settings_.beta * elevation_residual) / seconds)};
    updated = *state_;
  }
  return updated;
}

}  // namespace ics::tracker
