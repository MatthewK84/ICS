// config.h: the [trigger] table of a station's config (ICS-040), the
// production input of the trigger controller with the [cue] table (ICS-037).
// ics-trigd will read both. cpp/trigger/ics-trigger.toml is a sample with
// every setting:
//
//   [trigger]
//   address = "192.168.1.57"            # the 577's IPv4 address
//   port = 2101                         # its raw TCP port
//   reply_timeout_ns = 100_000_000      # the longest wait for a reply
//   channels = ["A", "B"]               # the 577's channels the cameras are on, A to H
//   width_ns = 100_000                  # each pulse's width
//   trigger_level = 2.5                 # volts the PPS must cross to trigger the 577
//   pps_offset_ns = 0                   # how long after the whole second the PPS reaches the 577
//   lead_ns = 0                         # how long before the closest approach the pulse goes out
//   command_margin_ns = 200_000_000     # how long before an edge the 577 must be armed for it
//   guard_ns = 50_000_000               # how long after an edge the 577 may be armed for the next
//   recommit_ns = 200_000               # a delay this far from the one programmed is programmed again
//   predict_period_ns = 100_000_000     # the least time between events that report a prediction
//   arm_timeout_ns = 120_000_000_000    # how long a sequence waits for its pulse
//   max_clock_bound_ns = 100_000        # the largest clock error bound a pulse is timed by
//   launch_speed = 3.0                  # m/s: a slower interceptor has not launched
//   horizon_ns = 60_000_000_000         # an approach further ahead than this is not predicted
//
// A channel that is not a letter from A to H reads as channel 0, which
// valid() refuses.
#ifndef ICS_TRIGGER_CONFIG_H
#define ICS_TRIGGER_CONFIG_H

// JSF deviation D-6: ics::config's Reader comes from its reader.hpp.
#include <ics/config/reader.hpp>
#include <ics/cue/extrapolator.h>
#include <ics/trigger/controller.h>
#include <ics/trigger/generator.h>
#include <ics/trigger/link.h>

namespace ics::trigger
{

struct Trigger_config
{
  Trigger_settings trigger;
  Link_settings link;
  Bnc577_settings generator;
};

// The [trigger] table, with the [cue] table's settings. Like every
// config::Reader schema, it records each missing or invalid setting and
// returns placeholders; use the settings only once config::Reader::finish()
// finds no errors, as config::read does.
[[nodiscard]] Trigger_config read_trigger_settings(config::Reader& trigger, const cue::Cue_settings& cue);

// The trigger's config: the [cue] and [trigger] tables.
[[nodiscard]] Trigger_config read_trigger_config(config::Reader& root);

}  // namespace ics::trigger

#endif  // ICS_TRIGGER_CONFIG_H
