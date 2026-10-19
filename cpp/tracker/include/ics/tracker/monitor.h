// monitor.h: ics-tracker-monitor (ICS-038), the operational test of the
// tracking camera and the centroider before ics-mountd (ICS-041) closes the
// loop on hardware. It streams the camera named in the [tracker] table and
// writes, for each frame, a JSON line with what it found: the target's
// centroid in pixels, its offset from the boresight on the sky in
// microradians, its area and peak, and the sky's level and noise. It searches
// the acquisition window around the boresight, and the track window around
// the last detection while there is one. A station checks its camera, focus,
// polarity and boresight with it, pointed at a drone, a light or a star.
#ifndef ICS_TRACKER_MONITOR_H
#define ICS_TRACKER_MONITOR_H

#include <filesystem>
#include <ostream>
#include <span>

#include <ics/common/universal_types.h>
#include <ics/tracker/config.h>

namespace ics::tracker
{

// The tool's exit codes.
inline constexpr int32 exit_monitored = 0;
inline constexpr int32 exit_failed = 1;
inline constexpr int32 exit_usage = 2;

// The most frames one run monitors.
inline constexpr int64 max_frames = 1000000000;

// Runs the tool. arguments are main's: the program, then the number of
// frames to monitor, or --help. The config file holds the [tracker] table.
[[nodiscard]] int32 run_monitor(const std::span<const char* const> arguments,
                                const std::filesystem::path& config_file,
                                std::ostream& out,
                                std::ostream& errors);

// Monitors frames frames of the camera config names: exit_failed when the
// settings are not valid or the camera does not open or start.
[[nodiscard]] int32 monitor_camera(const Tracker_config& config,
                                   const int64 frames,
                                   std::ostream& out,
                                   std::ostream& errors);

}  // namespace ics::tracker

#endif  // ICS_TRACKER_MONITOR_H
