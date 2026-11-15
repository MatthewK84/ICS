// queue.h: the engagements in the order they will happen, and the one a
// station serves (ICS-039), as python/ics_models/handoff/queue.py keeps them.
// A run plan lists its engagements. Each update takes what the station knows
// of every engagement now, a sighting: its predicted closest approach, when
// there is one, and the target's direction, when the station has it.
//
// - Done. An engagement is done when the approach it held has passed, within
//   the miss distance that makes it an engagement. A wider pass is a fly-by,
//   and the engagement goes on waiting for another approach. A sighting loses
//   its approach as the two vehicles pass, so an approach within the margin
//   of now is held until it passes.
// - Order. Engagements with an approach ahead come first, soonest first,
//   and those without follow, in the plan's order.
// - Serving. The station serves the first in the order. It stays on a done
//   engagement for the dwell, to see what the engagement did. It leaves one
//   that is not done only for an engagement more than the margin sooner, so
//   noise cannot flip it, or for any with an approach when its own has none.
// - Flags. An engagement that follows the one before it by less than the
//   minimum gap is too close. One the mount cannot reach in the gap, after
//   the dwell, the slew and the time to settle, is out of reach. The one
//   before the first in the order is the engagement the station last served
//   to its end. A done engagement keeps the flags it had.
#ifndef ICS_HANDOFF_QUEUE_H
#define ICS_HANDOFF_QUEUE_H

// JSF deviation D-6: ics::Duration, ics::UtcTime and ics::encoder::Direction come from .hpp headers.
#include <array>
#include <cstddef>
#include <optional>

#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/encoder/pointing_model.hpp>
#include <ics/handoff/approach.h>
#include <ics/handoff/slew.h>

namespace ics::handoff
{

// The most engagements a run plan holds.
inline constexpr int32 max_engagements = 8;
inline constexpr std::size_t engagements_length = static_cast<std::size_t>(max_engagements);

// The queue's times, and the miss distance that makes an engagement.
struct Queue_settings
{
  // An engagement nearer than this to the one before it is too close.
  Duration min_gap;
  // How long the station stays on a done engagement.
  Duration dwell;
  // How much sooner another engagement must be to take the station.
  Duration margin;
  // The time allowed to acquire the target after a slew.
  Duration settle;
  // A closest approach within this is an engagement, in metres.
  float64 engaged_miss_m;
};

// Whether no time is negative and the miss distance is positive.
[[nodiscard]] bool valid(const Queue_settings& settings);

// What a station knows of an engagement now.
struct Sighting
{
  // The closest approach ahead, when there is one to predict.
  std::optional<Approach> approach;
  // The target's direction from the station, when it has one.
  std::optional<encoder::Direction> direction;
};

// A sighting of each engagement, in the plan's order.
using Sightings = std::array<Sighting, engagements_length>;

// An engagement's place in the queue.
struct Slot
{
  // Whether the engagement has happened.
  bool done;
  // Whether it has an approach: the one ahead, or, once done, the one it had.
  bool timed;
  // That approach.
  Approach approach;
  // Whether it follows the one before it by less than the minimum gap.
  bool too_close;
  // Whether the mount cannot reach it in the gap.
  bool out_of_reach;
};

// The queue after an update.
struct Plan
{
  // Every engagement's slot, in the plan's order, and how many there are.
  std::array<Slot, engagements_length> slots;
  int32 count;
  // The engagements not done, next first, and how many there are.
  std::array<int32, engagements_length> order;
  int32 waiting;
  // The engagement the station serves, if any.
  std::optional<int32> current;
  // Whether that engagement is done, and the station is staying to see what it did.
  bool dwelling;
};

// A station's queue of a run plan's engagements.
class Queue
{
public:
  // A queue of count engagements, none with an approach yet. The settings
  // and the limits must be valid(), and count 1 to max_engagements: for any
  // other count a check fails and the queue holds one engagement.
  Queue(const int32 count,
        const Queue_settings& settings,
        const Slew_limits& limits);

  // The plan at a time, for a sighting of each engagement.
  [[nodiscard]] Plan update(const UtcTime now, const Sightings& sightings);

private:
  // Puts the engagements not done in order.
  void put_in_order();
  // Sets the engagement served, and whether the station is dwelling on it.
  void choose(const UtcTime now);
  // Flags each engagement not done against the one before it.
  void flag(const Sightings& sightings);

  Queue_settings settings_;
  Slew_limits limits_;
  Plan plan_;
  // The engagement the station last served to its end.
  std::optional<int32> previous_;
};

}  // namespace ics::handoff

#endif  // ICS_HANDOFF_QUEUE_H
