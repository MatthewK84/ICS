// shot.h: the PPS edge a pulse is timed from, and the delay from it
// (ICS-040), as python/ics_models/trigger/shot.py gives them.
//
// The pulse generator is triggered by the station's PPS, and puts its pulse a
// programmed delay after the edge. So the pulse's time does not depend on the
// network or the host: only on the edge, which is a whole UTC second plus the
// PPS's known offset, and on the delay.
//
// A pulse is timed from the last edge at or before it, so its delay is under
// a period. The generator fires on the first edge after it is armed, so it
// may be armed for an edge only after the edge before has passed, by a guard,
// and it must be armed a command margin before its own. When it is too late
// to arm for a pulse's own edge, the pulse goes out late: on the first edge
// that can still be armed for, with no delay.
#ifndef ICS_TRIGGER_SHOT_H
#define ICS_TRIGGER_SHOT_H

// JSF deviation D-6: ics::Duration and ics::UtcTime come from units.hpp.
#include <ics/common/units.hpp>

namespace ics::trigger
{

// The PPS, and the margins around it.
struct Shot_settings
{
  // The time between edges: a second.
  Duration period;
  // How long after the whole second an edge reaches the generator.
  Duration offset;
  // How long before an edge the generator must be armed for it.
  Duration command_margin;
  // How long after an edge the generator may be armed for the next.
  Duration guard;
};

// Whether the margins are not negative and the period holds both.
[[nodiscard]] bool valid(const Shot_settings& settings);

// A pulse.
struct Shot
{
  // The edge it is timed from.
  UtcTime edge;
  // The delay from that edge.
  Duration delay;
  // Whether it goes out later than it was asked for.
  bool late;
};

// When a shot's pulse goes out.
[[nodiscard]] UtcTime fire_time(const Shot& shot);

// The last PPS edge at or before a time. The settings must be valid().
[[nodiscard]] UtcTime edge_at_or_before(const UtcTime time, const Shot_settings& settings);

// The shot for a pulse asked for at a time, as it can be made from now.
[[nodiscard]] Shot shot_for(const UtcTime fire,
                            const UtcTime now,
                            const Shot_settings& settings);

// Whether the generator may be armed for a shot now: after the edge before
// it, and in time for its own.
[[nodiscard]] bool window_open(const Shot& shot,
                               const UtcTime now,
                               const Shot_settings& settings);

}  // namespace ics::trigger

#endif  // ICS_TRIGGER_SHOT_H
