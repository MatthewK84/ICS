// scheduler.cpp: the hand-off scheduler for one station (ICS-039).
// JSF deviation D-6: ics::Result, ics::UtcTime, ics::encoder, ics::frames and ics::mount come from .hpp headers.
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/extrapolator.h>
#include <ics/cue/filter.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/measurement.h>
#include <ics/cue/pointing.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/encoder/track.hpp>
#include <ics/frames/enu.hpp>
#include <ics/frames/geodetic.hpp>
#include <ics/handoff/approach.h>
#include <ics/handoff/queue.h>
#include <ics/handoff/scheduler.h>
#include <ics/handoff/slew.h>
#include <ics/mount/limits.hpp>
#include <ics/mount/pwi4_status.hpp>
#include <ics/mount/rate_loop.hpp>
#include <ics/tracker/camera.h>
#include <ics/tracker/demand.h>
#include <ics/tracker/estimator.h>
#include <ics/tracker/tracker.h>
#include <ics/v1/pli.pb.h>

namespace
{

using ics::int32;

// A configured site as a geodetic point.
ics::Result<ics::frames::Geodetic> point_of(const ics::cue::Site& site)
{
  return ics::frames::Geodetic::make(ics::Degrees(site.latitude_deg),
                                     ics::Degrees(site.longitude_deg),
                                     ics::Meters(site.height_m));
}

// Whether an engagement names two vehicles that differ.
bool paired(const ics::handoff::Pairing& pairing)
{
  const bool named = (!pairing.target.empty()) && (!pairing.interceptor.empty());
  return named && (pairing.target != pairing.interceptor);
}

// A cue as the tracker takes it.
ics::tracker::Angle_state state_of(const ics::cue::Cue& cue)
{
  return ics::tracker::Angle_state{cue.time,
                                   cue.azimuth_rad,
                                   cue.elevation_rad,
                                   cue.azimuth_rate_rad_s,
                                   cue.elevation_rate_rad_s};
}

}  // namespace

namespace ics::handoff
{

Status valid(const Handoff_settings& settings)
{
  bool usable = (settings.count >= 1) && (settings.count <= max_engagements);
  for (int32 engagement = 0; usable && (engagement < settings.count); ++engagement)
  {
    usable = paired(settings.pairings[static_cast<std::size_t>(engagement)]);
  }
  const bool timed = valid(settings.approach) && valid(settings.queue);
  const bool steered = valid(settings.slew) && (settings.cue.command_latency >= Duration::zero());
  Status status{};
  if (!(usable && timed && steered))
  {
    status = fail(Error::kInvalidArgument);
  }
  return status;
}

Scheduler::Scheduler(const Handoff_settings& settings,
                     const frames::EnuFrame& range,
                     const cue::Station& station,
                     const encoder::PointingModel& model,
                     tracker::Axis_source& axes,
                     tracker::Time_source& time)
    : settings_(settings),
      range_(range),
      station_(station),
      model_(model),
      axes_(axes),
      time_(time),
      filters_(static_cast<std::size_t>(2 * settings.count), cue::Cue_filter(settings.cue.filter)),
      queue_(settings.count, settings.queue, settings.slew),
      plan_(queue_.update(UtcTime{}, Sightings{}))
{
}

Result<std::unique_ptr<Scheduler>> Scheduler::make(const Handoff_settings& settings,
                                                   const tracker::Tracker_settings& tracking,
                                                   const encoder::PointingModel& model,
                                                   tracker::Axis_source& axes,
                                                   tracker::Time_source& time)
{
  Result<std::unique_ptr<Scheduler>> made = fail(Error::kInvalidArgument);
  const Result<frames::Geodetic> origin = point_of(settings.cue.range_origin);
  const Result<frames::Geodetic> site = point_of(settings.cue.station);
  if (!origin.has_value())
  {
    made = fail(origin.error());
  }
  else if (!site.has_value())
  {
    made = fail(site.error());
  }
  else if (valid(settings).has_value())
  {
    const frames::EnuFrame range(*origin);
    std::unique_ptr<Scheduler> scheduler(
      new Scheduler(settings, range, cue::station_at(range, *site), model, axes, time));
    Result<std::unique_ptr<tracker::Tracker>> tracker =
      tracker::Tracker::make(tracking, model, axes, *scheduler, time);
    if (tracker.has_value())
    {
      scheduler->tracker_ = std::move(*tracker);
      made = std::move(scheduler);
    }
  }
  else
  {
    // The settings cannot be used: kInvalidArgument.
  }
  return made;
}

const std::string& Scheduler::name_of(const int32 vehicle) const
{
  const Pairing& pairing = settings_.pairings[static_cast<std::size_t>(vehicle / 2)];
  return ((vehicle % 2) == 0) ? pairing.target : pairing.interceptor;
}

const cue::Cue_filter& Scheduler::filter_of(const int32 vehicle) const
{
  return filters_[static_cast<std::size_t>(vehicle)];
}

Take Scheduler::take(const v1::PliRecord& record)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  const Result<cue::Measurement> report = cue::measurement_from(record, range_, settings_.cue.defaults);
  Take taken = Take::unlisted;
  for (int32 vehicle = 0; vehicle < (2 * settings_.count); ++vehicle)
  {
    if (name_of(vehicle) == record.entity_id())
    {
      taken = Take::unusable;
      if (report.has_value())
      {
        // The filter holds a report it gates or finds too old, and uses none it should not.
        static_cast<void>(filters_[static_cast<std::size_t>(vehicle)].add(*report));
        taken = Take::given;
      }
    }
  }
  counts_.given += (taken == Take::given) ? 1 : 0;
  counts_.unlisted += (taken == Take::unlisted) ? 1 : 0;
  counts_.unusable += (taken == Take::unusable) ? 1 : 0;
  return taken;
}

Sighting Scheduler::sighting_of(const int32 engagement, const UtcTime now) const
{
  const Result<cue::Estimate> target = filter_of(2 * engagement).estimate_at(now);
  Sighting sighting{};
  if (target.has_value())
  {
    const Result<cue::Cue> cue = cue::cue_for(*target, station_);
    if (cue.has_value())
    {
      sighting.direction = encoder::Direction{cue->azimuth_rad, cue->elevation_rad};
    }
    const Result<cue::Estimate> interceptor = filter_of((2 * engagement) + 1).estimate_at(now);
    if (interceptor.has_value())
    {
      sighting.approach = closest_approach(*target, *interceptor, settings_.approach);
    }
  }
  return sighting;
}

bool Scheduler::advance(const UtcTime now)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  bool changed = false;
  if (now > advanced_)
  {
    Sightings sightings{};
    for (int32 engagement = 0; engagement < settings_.count; ++engagement)
    {
      sightings[static_cast<std::size_t>(engagement)] = sighting_of(engagement, now);
    }
    const std::optional<int32> served = plan_.current;
    plan_ = queue_.update(now, sightings);
    changed = plan_.current.has_value() && (plan_.current != served);
    counts_.handoffs += changed ? 1 : 0;
    advanced_ = now;
  }
  return changed;
}

Result<tracker::Angle_state> Scheduler::served_at(const UtcTime when) const
{
  Result<tracker::Angle_state> state = fail(Error::kEmpty);
  if (plan_.current.has_value())
  {
    const Result<cue::Estimate> estimate = filter_of(2 * (*plan_.current)).estimate_at(when);
    Result<cue::Cue> cue = fail(Error::kEmpty);
    if (estimate.has_value())
    {
      cue = cue::cue_for(*estimate, station_);
    }
    else
    {
      cue = fail(estimate.error());
    }
    if (cue.has_value())
    {
      state = state_of(*cue);
    }
    else
    {
      state = fail(cue.error());
    }
  }
  return state;
}

Result<tracker::Angle_state> Scheduler::cue_at(const UtcTime when)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return served_at(when);
}

void Scheduler::steer(const UtcTime now,
                      const UtcTime seen,
                      const bool searching)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  const UtcTime effect = now + settings_.cue.command_latency;
  slew_.reset();
  Result<tracker::Angle_state> target = fail(Error::kEmpty);
  if (searching)
  {
    target = served_at(effect);
  }
  const Result<encoder::Interpolated> axes = axes_.axes_at(seen);
  Result<encoder::Direction> axis_target = fail(Error::kEmpty);
  if (target.has_value() && axes.has_value())
  {
    axis_target = encoder::to_encoder(model_, encoder::Direction{target->azimuth_rad, target->elevation_rad});
  }
  if (axis_target.has_value())
  {
    const tracker::Angle_state aim{effect,
                                   axis_target->azimuth_rad,
                                   axis_target->elevation_rad,
                                   target->azimuth_rate_rad_s,
                                   target->elevation_rate_rad_s};
    const tracker::Angle_state mount{seen,
                                     axes->encoder.azimuth_rad,
                                     axes->encoder.elevation_rad,
                                     axes->azimuth_rate_rad_per_s,
                                     axes->elevation_rate_rad_per_s};
    const tracker::Axis_rates rates = slew_demand(aim, mount, settings_.slew);
    mount::AxisRates sent{};
    sent.rad_per_s[static_cast<std::size_t>(mount::Axis::kAzimuth)] = rates.azimuth_rad_s;
    sent.rad_per_s[static_cast<std::size_t>(mount::Axis::kAltitude)] = rates.elevation_rad_s;
    slew_ = mount::RateDemand{sent, now};
  }
}

tracker::Take Scheduler::take(const tracker::Frame& frame)
{
  const UtcTime now = time_.now();
  if (advance(now))
  {
    tracker_->retarget();
  }
  // The tracker asks for its cue as it takes the frame, so no lock is held here.
  const tracker::Take taken = tracker_->take(frame);
  const tracker::Mode mode = tracker_->mode();
  steer(now, frame.time, (mode == tracker::Mode::cueing) || (mode == tracker::Mode::acquiring));
  return taken;
}

std::optional<mount::RateDemand> Scheduler::slew() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return slew_;
}

std::optional<mount::RateDemand> Scheduler::latest()
{
  std::optional<mount::RateDemand> demand = slew();
  if (!demand.has_value())
  {
    demand = tracker_->latest();
  }
  return demand;
}

Handoff_status Scheduler::status() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return Handoff_status{plan_, slew_.has_value(), counts_};
}

tracker::Mode Scheduler::mode() const
{
  return tracker_->mode();
}

tracker::Tracker_counts Scheduler::tracker_counts() const
{
  return tracker_->counts();
}

}  // namespace ics::handoff
