// replay.h: ics-cue-replay's work (ICS-037), the operational test of the cue.
// It reads the PLI segment files (.icspli) in a folder, as ics-plid writes
// them (ICS-030), live or after a run, and replays their records in the order
// they were received. Every 20 ms of that time it writes the station's cue as
// one JSON line, with the counts of what became of the records:
//
//   {"time_utc_ns":...,"ok":true,"azimuth_rad":...,"elevation_rad":...,
//    "azimuth_rate_rad_s":...,"elevation_rate_rad_s":...,"range_m":...,
//    "sigma_rad":...,"taken":...,"late":...,"gated":...,"too_old":...,
//    "not_target":...,"unusable":...} on one line
//
// A cue that cannot be given has "ok":false and an "error" naming why, such
// as "unconstrained" past the coast limit. The tool reads its whole input
// before the replay, which then allocates nothing.
#ifndef ICS_CUE_REPLAY_H
#define ICS_CUE_REPLAY_H

#include <filesystem>
#include <ostream>
#include <span>

#include <ics/common/universal_types.h>
#include <ics/cue/extrapolator.h>

namespace ics::cue
{

// The exit codes.
constexpr int32 exit_replayed = 0;
constexpr int32 exit_failed = 1;
constexpr int32 exit_usage = 2;

// Runs ics-cue-replay with its arguments (the program's name first), the
// config file and the folder of segment files, writing cues to out and
// problems to errors. Returns an exit code: exit_replayed, exit_failed when the
// config, the extrapolator or the segments cannot be used, or exit_usage.
[[nodiscard]] int32 run_replay(const std::span<const char* const> arguments,
                               const std::filesystem::path& config_file,
                               const std::filesystem::path& folder,
                               std::ostream& out,
                               std::ostream& errors);

// Replays the segment files in a folder through an extrapolator with settings,
// writing cues to out and problems to errors. Returns exit_replayed, or
// exit_failed when the settings' sites are no points or the folder holds no
// readable records.
[[nodiscard]] int32 replay_folder(const Cue_settings& settings,
                                  const std::filesystem::path& folder,
                                  std::ostream& out,
                                  std::ostream& errors);

}  // namespace ics::cue

#endif  // ICS_CUE_REPLAY_H
