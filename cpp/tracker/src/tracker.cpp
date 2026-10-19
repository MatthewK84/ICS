// tracker.cpp: the closed-loop tracker (ICS-038).
// JSF deviation D-6: ics::Result, ics::encoder and ics::mount come from .hpp headers.
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/encoder/track.hpp>
#include <ics/mount/limits.hpp>
#include <ics/mount/pwi4_status.hpp>
#include <ics/mount/rate_loop.hpp>
#include <ics/tracker/camera.h>
#include <ics/tracker/centroid.h>
#include <ics/tracker/demand.h>
#include <ics/tracker/estimator.h>
#include <ics/tracker/geometry.h>
#include <ics/tracker/tracker.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::tracker::Extent;
using ics::tracker::max_window_side;
using ics::tracker::min_window_side;

// Whether a window has sides the centroider takes.
bool sized(const Extent extent)
{
  const bool wide = (extent.width >= min_window_side) && (extent.width <= max_window_side);
  return wide && (extent.height >= min_window_side) && (extent.height <= max_window_side);
}

// Whether a number is finite and positive.
bool positive(const float64 value)
{
  return std::isfinite(value) && (value > 0.0);
}

}  // namespace

namespace ics::tracker
{

UtcTime System_time::now()
{
  return std::chrono::time_point_cast<Duration>(std::chrono::system_clock::now());
}

Status valid(const Tracker_settings& settings)
{
  const Centroid_settings& centroid = settings.centroid;
  const Camera_geometry& camera = settings.camera;
  const bool placed = std::isfinite(camera.boresight_x) && std::isfinite(camera.boresight_y);
  const bool scaled = positive(camera.plate_scale_rad) && std::isfinite(camera.roll_rad);
  const bool areas = (centroid.min_area >= 1) && (centroid.min_area <= centroid.max_area);
  const bool levels = (centroid.k_sigma >= 0.0) && (centroid.min_contrast >= 0.0);
  const bool windows = sized(settings.acquire_window) && sized(settings.track_window);
  const bool gated = positive(settings.gate_px) && (settings.confirm_frames >= 1);
  const bool timed = (settings.coast_limit >= Duration::zero()) && (settings.command_latency >= Duration::zero());
  const bool steered = std::isfinite(settings.gain) && (settings.gain >= 0.0);
  const bool all = placed && scaled && areas && levels && windows && gated && timed && steered;
  Status status{};
  if (!(all && stable(settings.estimator)))
  {
    status = fail(Error::kInvalidArgument);
  }
  return status;
}

Tracker::Tracker(const Tracker_settings& settings,
                 const encoder::PointingModel& model,
                 Axis_source& axes,
                 Cue_source& cue,
                 Time_source& time)
    : settings_(settings),
      model_(model),
      axes_(axes),
      cue_(cue),
      time_(time),
      centroider_(settings.centroid),
      estimator_(settings.estimator)
{
}

Result<std::unique_ptr<Tracker>> Tracker::make(const Tracker_settings& settings,
                                               const encoder::PointingModel& model,
                                               Axis_source& axes,
                                               Cue_source& cue,
                                               Time_source& time)
{
  Result<std::unique_ptr<Tracker>> made = fail(Error::kInvalidArgument);
  if (valid(settings).has_value())
  {
    made = std::unique_ptr<Tracker>(new Tracker(settings, model, axes, cue, time));
  }
  return made;
}

Take Tracker::take(const Frame& frame)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  const UtcTime now = time_.now();
  const Result<encoder::Interpolated> axes = axes_.axes_at(frame.time);
  Take taken = Take::unusable;
  if (axes.has_value())
  {
    const encoder::Direction sight = encoder::to_sky(model_, axes->encoder);
    taken = look(frame, sight);
    steer(now,
          Angle_state{frame.time,
                      axes->encoder.azimuth_rad,
                      axes->encoder.elevation_rad,
                      axes->azimuth_rate_rad_per_s,
                      axes->elevation_rate_rad_per_s});
  }
  counts_.frames += 1;
  counts_.detections += (taken == Take::detected) ? 1 : 0;
  counts_.misses += (taken == Take::missed) ? 1 : 0;
  counts_.unusable += (taken == Take::unusable) ? 1 : 0;
  return taken;
}

Result<Angle_state> Tracker::expected_at(const UtcTime time)
{
  const bool cueing = mode_ == Mode::cueing;
  Result<Angle_state> expected = cueing ? cue_.cue_at(time) : estimator_.state();
  if (expected.has_value() && !cueing)
  {
    expected = carried(*expected, time);
  }
  return expected;
}

Take Tracker::look(const Frame& frame, const encoder::Direction& sight)
{
  const Result<Angle_state> expected = expected_at(frame.time);
  Result<Pixel_point> centre = Pixel_point{settings_.camera.boresight_x, settings_.camera.boresight_y};
  if (expected.has_value())
  {
    centre = pixel_of_sky({expected->azimuth_rad, expected->elevation_rad}, sight, settings_.camera);
  }
  const Extent size = (mode_ == Mode::cueing) ? settings_.acquire_window : settings_.track_window;
  Result<Window> window = fail(Error::kOutOfRange);
  if (centre.has_value())
  {
    window = window_around(*centre, size, Extent{frame.width, frame.height});
  }
  Result<Detection> detection = fail(Error::kOutOfRange);
  if (window.has_value())
  {
    detection = centroider_.find(frame, *window, *centre);
  }
  bool gated = false;
  if (detection.has_value())
  {
    const float64 distance = std::hypot(detection->x - centre->x, detection->y - centre->y);
    gated = (mode_ == Mode::cueing) || (distance <= settings_.gate_px);
  }
  Take taken = Take::missed;
  if (gated)
  {
    const encoder::Direction measured = sky_of_pixel({detection->x, detection->y}, sight, settings_.camera);
    taken = found(frame.time, measured, expected);
  }
  else
  {
    missed(frame.time);
  }
  return taken;
}

Take Tracker::found(const UtcTime time,
                    const encoder::Direction& measured,
                    const Result<Angle_state>& cue)
{
  Take taken = Take::detected;
  if (mode_ == Mode::cueing)
  {
    const float64 azimuth_rate = cue.has_value() ? cue->azimuth_rate_rad_s : 0.0;
    const float64 elevation_rate = cue.has_value() ? cue->elevation_rate_rad_s : 0.0;
    estimator_.start(Angle_state{time, measured.azimuth_rad, measured.elevation_rad, azimuth_rate, elevation_rate});
    confirmations_ = 1;
    mode_ = Mode::acquiring;
  }
  else if (estimator_.update(time, measured).has_value())
  {
    confirmations_ += 1;
    mode_ = (mode_ == Mode::coasting) ? Mode::tracking : mode_;
  }
  else
  {
    taken = Take::unusable;
  }
  if ((mode_ == Mode::acquiring) && (confirmations_ >= settings_.confirm_frames))
  {
    mode_ = Mode::tracking;
    counts_.acquisitions += 1;
  }
  return taken;
}

void Tracker::missed(const UtcTime time)
{
  if (mode_ == Mode::acquiring)
  {
    mode_ = Mode::cueing;
  }
  else if (mode_ == Mode::tracking)
  {
    mode_ = Mode::coasting;
    coast_start_ = time;
  }
  else if ((mode_ == Mode::coasting) && ((time - coast_start_) > settings_.coast_limit))
  {
    mode_ = Mode::cueing;
    counts_.losses += 1;
  }
  else
  {
    confirmations_ = 0;
  }
}

void Tracker::steer(const UtcTime now, const Angle_state& mount)
{
  const UtcTime effect = now + settings_.command_latency;
  const Result<Angle_state> target = expected_at(effect);
  Result<encoder::Direction> axis_target = fail(Error::kEmpty);
  if (target.has_value())
  {
    axis_target = encoder::to_encoder(model_, encoder::Direction{target->azimuth_rad, target->elevation_rad});
  }
  if (axis_target.has_value())
  {
    const Angle_state aim{effect,
                          axis_target->azimuth_rad,
                          axis_target->elevation_rad,
                          target->azimuth_rate_rad_s,
                          target->elevation_rate_rad_s};
    const Axis_rates rates = demand(aim, mount, settings_.gain);
    mount::AxisRates sent{};
    sent.rad_per_s[static_cast<std::size_t>(mount::Axis::kAzimuth)] = rates.azimuth_rad_s;
    sent.rad_per_s[static_cast<std::size_t>(mount::Axis::kAltitude)] = rates.elevation_rad_s;
    demand_ = mount::RateDemand{sent, now};
  }
}

std::optional<mount::RateDemand> Tracker::latest()
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return demand_;
}

Mode Tracker::mode() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return mode_;
}

Tracker_counts Tracker::counts() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return counts_;
}

}  // namespace ics::tracker
