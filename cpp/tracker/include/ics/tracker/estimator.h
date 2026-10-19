// estimator.h: the target's sky direction and rate, as an alpha-beta track
// (ICS-038), as python/ics_models/tracker/estimator.py keeps it. Each axis is
// tracked on its own. A measurement first predicts the track to its time at
// the track's rate; the residual, the measurement less the prediction (the
// shorter way round in azimuth), then moves the angle by alpha times itself
// and the rate by beta times itself over the time since the last. Alpha in
// (0, 1] and beta in [0, 4 - 2 alpha) keep the track stable.
#ifndef ICS_TRACKER_ESTIMATOR_H
#define ICS_TRACKER_ESTIMATOR_H

// JSF deviation D-6: ics::Result, ics::UtcTime and ics::encoder::Direction come from .hpp headers.
#include <optional>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>

namespace ics::tracker
{

// The track's gains.
struct Estimator_settings
{
  float64 alpha;
  float64 beta;
};

// Whether gains keep the track stable.
[[nodiscard]] bool stable(const Estimator_settings& settings);

// A direction and its rates, in radians and radians a second, at a UTC time.
// The tracker keeps the target's sky direction in it, and the mount's axis
// angles.
struct Angle_state
{
  UtcTime time;
  float64 azimuth_rad;
  float64 elevation_rad;
  float64 azimuth_rate_rad_s;
  float64 elevation_rate_rad_s;
};

// The state carried to another time at its rates.
[[nodiscard]] Angle_state carried(const Angle_state& state, const UtcTime time);

// An alpha-beta track of a direction.
class Angle_estimator
{
public:
  // A track not yet started; settings must be stable().
  explicit Angle_estimator(const Estimator_settings& settings);

  // Starts, or restarts, the track from a state.
  void start(const Angle_state& state);
  // The track as of its last measurement; kEmpty before start().
  [[nodiscard]] Result<Angle_state> state() const;
  // Takes a measurement and returns the track at its time: kEmpty before
  // start(), kOutOfRange for a measurement no later than the last.
  [[nodiscard]] Result<Angle_state> update(const UtcTime time, const encoder::Direction& measured);

private:
  Estimator_settings settings_;
  std::optional<Angle_state> state_;
};

}  // namespace ics::tracker

#endif  // ICS_TRACKER_ESTIMATOR_H
