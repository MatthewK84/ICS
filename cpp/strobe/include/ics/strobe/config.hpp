#pragma once

#include <filesystem>
#include <string>

#include "ics/config/logging_config.hpp"
#include "ics/config/reader.hpp"
#include "ics/strobe/analyze.hpp"

namespace ics::strobe {

// The settings of ics-strobe-analyzer (ICS-029), from its TOML file:
//
//   [log]
//   level = "info"
//   service = "ics-strobe-analyzer"
//
//   [strobe]
//   station_id = "station-1"                          # as named in the range configuration
//   camera_id = "phantom-1"                           # as named in the station configuration
//   offsets_file = "/var/lib/ics/camera-offsets.binpb" # ics-timingd's camera_offsets_file
//   roi_x_px = 600                                    # where the strobe's light falls
//   roi_y_px = 400
//   roi_width_px = 16
//   roi_height_px = 16
//   pulse_width_ns = 20_000                           # the strobe's pulse, 1 ns to 1 s
//   latency_ns = 1_500                                # from the PPS edge to light, 0 to 1 s
//   delay_step_ns = 5_000                             # the sweep's step, 0 to 1 s
//   sweep_steps = 40                                  # 1 (no sweep) to 3,600
//   max_offset_ns = 1_000_000                         # the search, 1 µs to 100 ms either side of 0
//
// A sweep pins the offset only where a pulse straddles an exposure's edge:
// a delay step shorter than the pulse makes one do so at every edge the
// sweep crosses.
struct AnalyzerConfig {
  config::LoggingConfig log;
  std::string station_id;
  StrobeSettings strobe;
  std::filesystem::path offsets_file;
};

// Reads the [log] and [strobe] tables under root. Whether the schedule is
// one a strobe keeps (camera::StrobeSchedule::valid) is left to the caller.
[[nodiscard]] AnalyzerConfig read_analyzer_config(config::Reader& root);

}  // namespace ics::strobe
