// shot.cpp: the PPS edge a pulse is timed from, and the delay from it (ICS-040).
// JSF deviation D-6: ics::check, ics::Duration and ics::UtcTime come from .hpp headers.
#include <chrono>

#include <ics/common/check.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/trigger/shot.h>

namespace ics::trigger
{

bool valid(const Shot_settings& settings)
{
  const bool margins = (settings.command_margin >= Duration::zero()) && (settings.guard >= Duration::zero());
  return margins && (settings.period > (settings.command_margin + settings.guard));
}

UtcTime fire_time(const Shot& shot)
{
  return shot.edge + shot.delay;
}

UtcTime edge_at_or_before(const UtcTime time, const Shot_settings& settings)
{
  static_cast<void>(check(settings.period > Duration::zero()));
  const int64 since = (time.time_since_epoch() - settings.offset).count();
  const int64 period = settings.period.count();
  int64 periods = since / period;
  if ((since % period) < 0)
  {
    // Division rounds towards zero: an edge before the first is one further back.
    --periods;
  }
  return UtcTime{} + Duration(periods * period) + settings.offset;
}

Shot shot_for(const UtcTime fire,
              const UtcTime now,
              const Shot_settings& settings)
{
  const UtcTime ready = now + settings.command_margin;
  Shot shot{edge_at_or_before(fire, settings), Duration::zero(), false};
  if (ready <= shot.edge)
  {
    shot.delay = fire - shot.edge;
  }
  else
  {
    shot.edge = edge_at_or_before(ready - std::chrono::nanoseconds(1), settings) + settings.period;
    shot.late = true;
  }
  return shot;
}

bool window_open(const Shot& shot,
                 const UtcTime now,
                 const Shot_settings& settings)
{
  const UtcTime opens = (shot.edge - settings.period) + settings.guard;
  return (opens <= now) && (now <= (shot.edge - settings.command_margin));
}

}  // namespace ics::trigger
