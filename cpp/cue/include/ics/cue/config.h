// config.h: the [cue] table of a station's config (ICS-037), the production
// input of the cue extrapolator. ics-cue-replay reads it from
// /etc/ics/ics-cue.toml, and ics-mountd (ICS-041) will read the same table.
// cpp/cue/ics-cue.toml is a sample with every setting:
//
//   [cue]
//   range_origin_latitude_deg = 40.0      # the range ENU origin, as every ICS module takes it
//   range_origin_longitude_deg = -100.0
//   range_origin_height_m = 700.0         # above the WGS 84 ellipsoid
//   station_latitude_deg = 39.9977477     # this station's mount, surveyed
//   station_longitude_deg = -100.0
//   station_height_m = 702.0
//   target_role = "target"                # the role the adapters give the target's reports
//   command_latency_ns = 50_000_000       # from sending a mount command to its taking effect
//   horizontal_jerk_psd = 4.0             # white-jerk density, m^2/s^5, east and north
//   vertical_jerk_psd = 1.0               # and up
//   gate = 16.27                          # chi-squared, 3 degrees of freedom: 16.27 is 99.9%
//   history_ns = 2_000_000_000            # how late a report may arrive and still be applied
//   coast_limit_ns = 2_000_000_000        # how long a cue may be predicted past the last report
//   capacity = 64                         # the most reports held, 1 to 64
//   initial_velocity_sigma = 20.0         # m/s, for a first report without a velocity
//   initial_acceleration_sigma = 10.0     # m/s^2, for a first report
//   velocity_sigma = 0.2                  # m/s, for every reported velocity
//   two_dimensional_horizontal_m = 3.0    # sigmas for a record that carries none, by fix type
//   two_dimensional_vertical_m = 10.0
//   three_dimensional_horizontal_m = 1.5
//   three_dimensional_vertical_m = 3.0
//   dgnss_horizontal_m = 0.7
//   dgnss_vertical_m = 1.5
//   rtk_float_horizontal_m = 0.3
//   rtk_float_vertical_m = 0.6
//   rtk_fixed_horizontal_m = 0.03
//   rtk_fixed_vertical_m = 0.05
//   other_horizontal_m = 5.0              # anything else, such as an unspecified radar fix
//   other_vertical_m = 5.0
#ifndef ICS_CUE_CONFIG_H
#define ICS_CUE_CONFIG_H

// JSF deviation D-6: ics::config's Reader comes from its reader.hpp.
#include <ics/config/reader.hpp>
#include <ics/cue/extrapolator.h>

namespace ics::cue
{

// The [cue] table. Like every config::Reader schema, it records each missing
// or invalid setting and returns placeholders; use the settings only once
// config::Reader::finish() finds no errors, as config::read does.
[[nodiscard]] Cue_settings read_cue_settings(config::Reader& cue);

// ics-cue-replay's config: the [cue] table, and nothing else.
[[nodiscard]] Cue_settings read_replay_config(config::Reader& root);

}  // namespace ics::cue

#endif  // ICS_CUE_CONFIG_H
