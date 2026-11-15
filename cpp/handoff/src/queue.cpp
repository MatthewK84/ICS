// queue.cpp: the engagements in order, and the one a station serves (ICS-039).
// JSF deviation D-6: ics::check, ics::Duration and ics::UtcTime come from .hpp headers.
#include <cstddef>
#include <optional>

#include <ics/common/check.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/handoff/approach.h>
#include <ics/handoff/queue.h>
#include <ics/handoff/slew.h>

namespace
{

using ics::float64;
using ics::int32;
using ics::handoff::Approach;
using ics::handoff::Plan;
using ics::handoff::Queue_settings;
using ics::handoff::Sighting;
using ics::handoff::Slot;

constexpr float64 nanoseconds_per_second = 1.0E9;

// A time span in seconds.
float64 seconds_of(const ics::Duration span)
{
  return static_cast<float64>(span.count()) / nanoseconds_per_second;
}

// An engagement's slot in a plan.
Slot& slot_of(Plan& plan, const int32 engagement)
{
  return plan.slots[static_cast<std::size_t>(engagement)];
}

// The engagement at a place in a plan's order.
int32& placed(Plan& plan, const int32 place)
{
  return plan.order[static_cast<std::size_t>(place)];
}

// A slot not done, after a sighting: done if its approach has passed near
// enough, else with the approach ahead.
Slot moved_on(const Slot& slot,
              const Sighting& sighting,
              const ics::UtcTime now,
              const Queue_settings& settings)
{
  Slot moved = slot;
  if (slot.timed && (slot.approach.time <= now) && (slot.approach.miss_m <= settings.engaged_miss_m))
  {
    moved.done = true;
  }
  else
  {
    const std::optional<Approach>& seen = sighting.approach;
    const bool ahead = seen.has_value() && (seen->time > now);
    const bool nearly = slot.timed && (slot.approach.time > now) && (slot.approach.time <= (now + settings.margin));
    if (ahead)
    {
      moved.approach = *seen;
    }
    moved.timed = ahead || nearly;
  }
  return moved;
}

// Whether the first engagement's approach is more than a margin before the
// second's, or the second has none.
bool sooner(const Slot& first,
            const Slot& second,
            const ics::Duration margin)
{
  bool is_sooner = false;
  if (first.timed)
  {
    is_sooner = (!second.timed) || ((first.approach.time + margin) < second.approach.time);
  }
  return is_sooner;
}

// The seconds the mount takes between two engagements' targets, when the
// station has both directions.
std::optional<float64> reach_between(const Sighting& first,
                                     const Sighting& second,
                                     const ics::handoff::Slew_limits& limits)
{
  std::optional<float64> reach;
  if (first.direction.has_value() && second.direction.has_value())
  {
    reach = ics::handoff::reach_seconds(*first.direction, *second.direction, limits);
  }
  return reach;
}

// Flags a slot with an approach against the engagement served before it,
// which has one too.
void flag_against(Slot& slot,
                  const Slot& before,
                  const std::optional<float64>& reach_s,
                  const Queue_settings& settings)
{
  static_cast<void>(ics::check(before.timed));
  const ics::Duration gap = slot.approach.time - before.approach.time;
  slot.too_close = gap < settings.min_gap;
  if (reach_s.has_value())
  {
    const float64 needed_s = seconds_of(settings.dwell + settings.settle) + *reach_s;
    slot.out_of_reach = needed_s > seconds_of(gap);
  }
}

}  // namespace

namespace ics::handoff
{

bool valid(const Queue_settings& settings)
{
  const bool gapped = (settings.min_gap >= Duration::zero()) && (settings.dwell >= Duration::zero());
  const bool timed = (settings.margin >= Duration::zero()) && (settings.settle >= Duration::zero());
  return gapped && timed && (settings.engaged_miss_m > 0.0);
}

Queue::Queue(const int32 count,
             const Queue_settings& settings,
             const Slew_limits& limits)
    : settings_(settings), limits_(limits), plan_(), previous_()
{
  static_cast<void>(check(valid(settings) && valid(limits)));
  plan_.count = check((count >= 1) && (count <= max_engagements)) ? count : 1;
}

Plan Queue::update(const UtcTime now, const Sightings& sightings)
{
  for (int32 engagement = 0; engagement < plan_.count; ++engagement)
  {
    Slot& slot = slot_of(plan_, engagement);
    if (!slot.done)
    {
      slot = moved_on(slot, sightings[static_cast<std::size_t>(engagement)], now, settings_);
    }
  }
  put_in_order();
  choose(now);
  flag(sightings);
  return plan_;
}

void Queue::put_in_order()
{
  plan_.waiting = 0;
  for (int32 engagement = 0; engagement < plan_.count; ++engagement)
  {
    if (!slot_of(plan_, engagement).done)
    {
      // An insertion that keeps the plan's order between engagements neither sooner than the other.
      int32 place = plan_.waiting;
      while ((place > 0) &&
             sooner(slot_of(plan_, engagement), slot_of(plan_, placed(plan_, place - 1)), Duration::zero()))
      {
        placed(plan_, place) = placed(plan_, place - 1);
        --place;
      }
      placed(plan_, place) = engagement;
      ++plan_.waiting;
    }
  }
}

void Queue::choose(const UtcTime now)
{
  plan_.dwelling = false;
  if (plan_.current.has_value() && slot_of(plan_, *plan_.current).done)
  {
    // A done engagement keeps the approach it had.
    static_cast<void>(check(slot_of(plan_, *plan_.current).timed));
    if (now < (slot_of(plan_, *plan_.current).approach.time + settings_.dwell))
    {
      plan_.dwelling = true;
    }
    else
    {
      previous_ = plan_.current;
      plan_.current.reset();
    }
  }
  // With nothing in the order, no engagement is served: one not done would be in it.
  if ((!plan_.dwelling) && (plan_.waiting > 0))
  {
    const int32 head = placed(plan_, 0);
    if ((!plan_.current.has_value()) || sooner(slot_of(plan_, head), slot_of(plan_, *plan_.current), settings_.margin))
    {
      plan_.current = head;
    }
  }
}

void Queue::flag(const Sightings& sightings)
{
  std::optional<int32> before = plan_.dwelling ? plan_.current : previous_;
  for (int32 place = 0; place < plan_.waiting; ++place)
  {
    const int32 engagement = placed(plan_, place);
    Slot& slot = slot_of(plan_, engagement);
    slot.too_close = false;
    slot.out_of_reach = false;
    // An engagement with an approach follows one with an approach: those without come last.
    if (before.has_value() && slot.timed)
    {
      const std::optional<float64> reach = reach_between(sightings[static_cast<std::size_t>(*before)],
                                                         sightings[static_cast<std::size_t>(engagement)],
                                                         limits_);
      flag_against(slot, slot_of(plan_, *before), reach, settings_);
    }
    before = engagement;
  }
}

}  // namespace ics::handoff
