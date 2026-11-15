// controller.cpp: the trigger controller for one station (ICS-040).
// JSF deviation D-6: ics::Result, ics::Duration, ics::UtcTime and ics::frames come from .hpp headers.
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/cue/extrapolator.h>
#include <ics/cue/filter.h>
#include <ics/cue/kinematics.h>
#include <ics/cue/measurement.h>
#include <ics/frames/enu.hpp>
#include <ics/frames/geodetic.hpp>
#include <ics/trigger/approach.h>
#include <ics/trigger/controller.h>
#include <ics/trigger/events.h>
#include <ics/trigger/generator.h>
#include <ics/trigger/shot.h>
#include <ics/v1/pli.pb.h>
#include <ics/v1/trigger_event.pb.h>

namespace
{

using ics::int32;
using ics::v1::TriggerEvent;

// A configured site as a geodetic point.
ics::Result<ics::frames::Geodetic> point_of(const ics::cue::Site& site)
{
  return ics::frames::Geodetic::make(ics::Degrees(site.latitude_deg),
                                     ics::Degrees(site.longitude_deg),
                                     ics::Meters(site.height_m));
}

// Whether the channels are 1 to 8, each named.
bool named(const ics::trigger::Trigger_settings& settings)
{
  bool all = (settings.channel_count >= 1) && (settings.channel_count <= ics::trigger::max_channels);
  for (int32 place = 0; all && (place < settings.channel_count); ++place)
  {
    all = !settings.channels[static_cast<std::size_t>(place)].empty();
  }
  return all;
}

}  // namespace

namespace ics::trigger
{

Status valid(const Trigger_settings& settings)
{
  const bool aimed = (settings.lead >= Duration::zero()) && (settings.recommit >= Duration::zero());
  const bool waited = (settings.predict_period >= Duration::zero()) && (settings.arm_timeout > Duration::zero());
  const bool timed = aimed && waited && (settings.max_clock_bound >= Duration::zero());
  const bool planned = valid(settings.approach) && valid(settings.shot);
  Status status{};
  if (!(named(settings) && timed && planned))
  {
    status = fail(Error::kInvalidArgument);
  }
  return status;
}

Controller::Controller(const Trigger_settings& settings,
                       const frames::EnuFrame& range,
                       Pulse_generator& generator,
                       Event_sink& sink)
    : settings_(settings),
      range_(range),
      generator_(generator),
      sink_(sink),
      target_filter_(settings.cue.filter),
      interceptor_filter_(settings.cue.filter)
{
}

Result<std::unique_ptr<Controller>> Controller::make(const Trigger_settings& settings,
                                                     Pulse_generator& generator,
                                                     Event_sink& sink)
{
  Result<std::unique_ptr<Controller>> made = fail(Error::kInvalidArgument);
  const Result<frames::Geodetic> origin = point_of(settings.cue.range_origin);
  if (!origin.has_value())
  {
    made = fail(origin.error());
  }
  else if (valid(settings).has_value())
  {
    made = std::unique_ptr<Controller>(new Controller(settings, frames::EnuFrame(*origin), generator, sink));
  }
  else
  {
    // The settings cannot be used: kInvalidArgument.
  }
  return made;
}

void Controller::emit(const TriggerEvent::Kind kind,
                      const UtcTime time,
                      const std::string& detail)
{
  TriggerEvent event;
  event.set_sequence_id(sequence_id_);
  event.set_time_utc_ns(time.time_since_epoch().count());
  event.set_kind(kind);
  event.set_target_entity_id(target_);
  event.set_interceptor_entity_id(interceptor_);
  if (kind == TriggerEvent::KIND_PREDICTED)
  {
    const Approach latest = approach_.value_or(Approach{});
    event.set_predicted_closest_approach_utc_ns(latest.time.time_since_epoch().count());
    event.set_predicted_miss_distance_m(latest.miss_m);
  }
  if (kind == TriggerEvent::KIND_FIRED)
  {
    // The prediction the shot was timed by.
    const Approach timed_by = aimed_.value_or(Approach{});
    event.set_predicted_closest_approach_utc_ns(timed_by.time.time_since_epoch().count());
    event.set_predicted_miss_distance_m(timed_by.miss_m);
    for (int32 place = 0; place < settings_.channel_count; ++place)
    {
      event.add_channels(settings_.channels[static_cast<std::size_t>(place)]);
    }
  }
  event.set_detail(detail);
  if (!sink_.put(event).has_value())
  {
    ++lost_events_;
  }
}

void Controller::end(const TriggerEvent::Kind kind,
                     const UtcTime now,
                     const std::string& detail)
{
  bool stopped = true;
  if (stage_ == Stage::committed)
  {
    stopped = generator_.disarm().has_value();
  }
  emit(kind, now, detail);
  if (!stopped)
  {
    emit(TriggerEvent::KIND_FAULT, now, "the pulse generator did not disarm");
  }
  stage_ = Stage::idle;
}

Status Controller::engage(const std::string& target,
                          const std::string& interceptor,
                          const UtcTime now)
{
  const bool paired = (!target.empty()) && (!interceptor.empty()) && (target != interceptor);
  Status status = fail(Error::kInvalidArgument);
  if (paired && (stage_ != Stage::idle))
  {
    status = fail(Error::kFull);
  }
  else if (paired)
  {
    target_ = target;
    interceptor_ = interceptor;
    target_filter_ = cue::Cue_filter(settings_.cue.filter);
    interceptor_filter_ = cue::Cue_filter(settings_.cue.filter);
    ++sequences_;
    sequence_id_ = "sequence-" + std::to_string(sequences_);
    armed_at_ = now;
    approach_.reset();
    aimed_.reset();
    reported_at_.reset();
    stage_ = Stage::armed;
    emit(TriggerEvent::KIND_ARMED, now, "");
    status = Status{};
  }
  else
  {
    // The names do not make a pair: kInvalidArgument.
  }
  return status;
}

void Controller::disarm(const std::string& why, const UtcTime now)
{
  if (stage_ != Stage::idle)
  {
    end(TriggerEvent::KIND_DISARMED, now, why);
  }
}

Take Controller::take(const v1::PliRecord& record)
{
  const bool of_target = record.entity_id() == target_;
  const bool of_interceptor = record.entity_id() == interceptor_;
  Take taken = Take::unlisted;
  if ((stage_ != Stage::idle) && (of_target || of_interceptor))
  {
    const Result<cue::Measurement> report = cue::measurement_from(record, range_, settings_.cue.defaults);
    taken = Take::unusable;
    if (report.has_value())
    {
      // The filter holds a report it gates or finds too old, and uses none it should not.
      cue::Cue_filter& filter = of_target ? target_filter_ : interceptor_filter_;
      static_cast<void>(filter.add(*report));
      taken = Take::given;
    }
  }
  return taken;
}

std::optional<Approach> Controller::predicted(const UtcTime now) const
{
  const Result<cue::Estimate> target = target_filter_.estimate_at(now);
  const Result<cue::Estimate> interceptor = interceptor_filter_.estimate_at(now);
  std::optional<Approach> approach;
  if (target.has_value() && interceptor.has_value())
  {
    approach = refined_approach(*target, *interceptor, settings_.approach);
  }
  return approach;
}

void Controller::commit(const UtcTime now, const Shot& wanted)
{
  Status status = generator_.program(wanted.delay);
  if (status.has_value() && (stage_ != Stage::committed))
  {
    status = generator_.arm();
  }
  // Either way the generator may be armed now, so a refusal's end disarms it.
  stage_ = Stage::committed;
  if (status.has_value())
  {
    shot_ = wanted;
    aimed_ = approach_;
  }
  else
  {
    end(TriggerEvent::KIND_FAULT, now, "the pulse generator refused the shot");
  }
}

void Controller::aim(const UtcTime now, const Shot& wanted)
{
  if ((stage_ == Stage::committed) && (wanted.edge != shot_.edge))
  {
    // The pulse has moved to another edge: this one must not start it.
    const bool stopped = generator_.disarm().has_value();
    stage_ = Stage::armed;
    if (!stopped)
    {
      end(TriggerEvent::KIND_FAULT, now, "the pulse generator did not disarm");
    }
  }
  const bool moved =
    (stage_ == Stage::armed) || (std::chrono::abs(wanted.delay - shot_.delay) > settings_.recommit);
  if ((stage_ != Stage::idle) && window_open(wanted, now, settings_.shot) && moved)
  {
    commit(now, wanted);
  }
}

void Controller::follow(const UtcTime now, const Approach& approach)
{
  approach_ = approach;
  const bool due = (!reported_at_.has_value()) || ((now - *reported_at_) >= settings_.predict_period);
  if (due)
  {
    reported_at_ = now;
    emit(TriggerEvent::KIND_PREDICTED, now, "");
  }
  // From a command margin before its edge, a committed shot stands.
  const bool stands = (stage_ == Stage::committed) && (now > (shot_.edge - settings_.shot.command_margin));
  if (!stands)
  {
    aim(now, shot_for(approach.time - settings_.lead, now, settings_.shot));
  }
}

void Controller::step(const UtcTime now, const Clock_state& clock)
{
  if (stage_ != Stage::idle)
  {
    const bool timed = clock.locked && (clock.bound <= settings_.max_clock_bound);
    const bool fired = (stage_ == Stage::committed) && (now >= fire_time(shot_));
    if (fired)
    {
      end(TriggerEvent::KIND_FIRED, fire_time(shot_), shot_.late ? "the pulse went out late" : "");
    }
    else if (!timed)
    {
      end(TriggerEvent::KIND_DISARMED, now, "the clock is not locked within its bound");
    }
    else if ((now - armed_at_) > settings_.arm_timeout)
    {
      end(TriggerEvent::KIND_DISARMED, now, "no pulse went out before the arming timeout");
    }
    else
    {
      const std::optional<Approach> approach = predicted(now);
      if (approach.has_value())
      {
        follow(now, *approach);
      }
    }
  }
}

Stage Controller::stage() const
{
  return stage_;
}

std::optional<Shot> Controller::shot() const
{
  std::optional<Shot> held;
  if (stage_ == Stage::committed)
  {
    held = shot_;
  }
  return held;
}

int64 Controller::lost_events() const
{
  return lost_events_;
}

}  // namespace ics::trigger
