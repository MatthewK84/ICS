// config.h: the [handoff] table of a station's config (ICS-039), the
// production input of the hand-off scheduler with the [cue] table (ICS-037).
// ics-mountd (ICS-041) will read both. cpp/handoff/ics-handoff.toml is a
// sample with every setting:
//
//   [handoff]
//   targets = ["uas-1", "uas-2"]        # each engagement's target, as PliRecord.entity_id, in the run plan's order
//   interceptors = ["int-1", "int-2"]   # and the interceptor sent at it, in the same order
//   min_gap_ns = 3_000_000_000          # an engagement nearer than this to the one before it is flagged
//   dwell_ns = 500_000_000              # how long the station stays on an engagement after its closest approach
//   margin_ns = 500_000_000             # how much sooner another engagement must be to take the station
//   settle_ns = 300_000_000             # the time allowed to acquire a target after a slew
//   engaged_miss_m = 10.0               # a closest approach within this is an engagement
//   launch_speed = 3.0                  # m/s: a slower interceptor has not launched
//   horizon_ns = 60_000_000_000         # an approach further ahead than this is not predicted
//   slew_rate = 0.17                    # rad/s, the fastest a slew turns an axis
//   slew_acceleration = 0.5             # rad/s^2, the deceleration a slew is planned on
//   slew_gain = 6.0                     # a second, the gain that takes over near the target
//
// The two lists must be the same length, at most 8: a list shorter than the
// other leaves engagements with a vehicle unnamed, which valid() refuses.
#ifndef ICS_HANDOFF_CONFIG_H
#define ICS_HANDOFF_CONFIG_H

// JSF deviation D-6: ics::config's Reader comes from its reader.hpp.
#include <ics/config/reader.hpp>
#include <ics/cue/extrapolator.h>
#include <ics/handoff/scheduler.h>

namespace ics::handoff
{

// The [handoff] table, with the [cue] table's settings. Like every
// config::Reader schema, it records each missing or invalid setting and
// returns placeholders; use the settings only once config::Reader::finish()
// finds no errors, as config::read does.
[[nodiscard]] Handoff_settings read_handoff_settings(config::Reader& handoff, const cue::Cue_settings& cue);

// The scheduler's config: the [cue] and [handoff] tables.
[[nodiscard]] Handoff_settings read_handoff_config(config::Reader& root);

}  // namespace ics::handoff

#endif  // ICS_HANDOFF_CONFIG_H
