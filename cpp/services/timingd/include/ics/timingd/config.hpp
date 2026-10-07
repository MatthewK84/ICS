#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "ics/common/units.hpp"
#include "ics/config/logging_config.hpp"
#include "ics/config/reader.hpp"
#include "ics/timing/quality.hpp"

namespace ics::timingd {

// The settings of ics-timingd (ICS-019), from its TOML file:
//
//   [log]
//   level = "info"
//   service = "ics-timingd"
//
//   [timing]
//   station_id = "station-1"                        # as named in the range configuration
//   ptp4l_socket = "/var/run/ptp4l-ro"              # ptp4l's read-only management socket
//   client_socket = "/run/ics-timingd/ptp4l-client" # this end of it
//   publish_socket = "/run/ics-timingd/time-quality"
//   camera_offsets_file = "/var/lib/ics/camera-offsets.binpb" # the strobe calibration's (ICS-029)
//   ptp_domain = 0
//   poll_interval_ns = 100_000_000                  # 10 ms to 1 s
//   asymmetry_bound_ns = 1_000                      # 0 to 1 s
//   holdover_drift_ns_per_s = 50.0                  # 0 to 1e6
struct Config {
  config::LoggingConfig log;
  std::string station_id;
  std::filesystem::path ptp4l_socket;
  std::filesystem::path client_socket;
  std::filesystem::path publish_socket;
  // The offsets file the strobe analyzer writes (timing::CameraOffsets).
  std::filesystem::path camera_offsets_file;
  std::uint8_t ptp_domain = 0;
  // How often ptp4l is polled and a report published; each poll waits this
  // long for ptp4l's answers.
  Duration poll_interval{};
  timing::ErrorModel model;
};

// Reads the [log] and [timing] tables under root.
[[nodiscard]] Config read_config(config::Reader& root);

}  // namespace ics::timingd
