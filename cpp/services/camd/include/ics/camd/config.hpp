#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ics/camd/drivers.hpp"
#include "ics/camera/segment_camera.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/config/logging_config.hpp"
#include "ics/config/reader.hpp"

namespace ics::camd {

// The most cameras one station's ics-camd drives.
inline constexpr std::size_t kMaxCameras = 16;

// One camera, from its [camera.ID] table:
//
//   [camera.north-hs]
//   width_px = 64                   # the image, 1 to 65,535
//   height_px = 32
//   window_x_px = 0                 # the sensor window's offset, 0 to 65,535
//   window_y_px = 0
//   frame_rate_hz = 5_000           # 1 to 1,000,000
//   exposure_ns = 150_000           # 1 ns to 1 s
//   segment_frames = 500            # 1 to 2^20
//   post_trigger_frames = 400       # 0 to 2^20
//   irig = true                     # whether its clock follows IRIG-B
//   segments = 2                    # recorded each run, 1 to 64
//   first_frame = -50               # the frames saved of each, by number
//   frame_count = 300               # from its trigger frame; 1 to 2^20
//
//   [camera.north-hs.driver]        # see drivers.hpp
//   name = "emulated-phantom"
//   trigger_interval_ns = 120_000_000
struct CameraConfig {
  std::string id;
  camera::CameraSettings settings;
  std::uint32_t segments = 0;
  camera::FrameRange frames{};
  DriverSettings driver;
};

// The settings of ics-camd (ICS-031), from its TOML file:
//
//   [log]
//   level = "info"
//   service = "ics-camd"
//
//   [camd]
//   station_id = "station-1"                    # as named in the range configuration
//   command_socket = "/run/ics-camd/command"    # where CameraService is served
//   time_socket = "/run/ics-timingd/time-quality"  # ics-timingd's reports
//   offload_folder = "/var/lib/ics/cameras"     # each run's folder goes here
//   time_wait_ns = 2_000_000_000                # for the first report, 0 to 10 s
//   cameras = ["north-hs", "north-mwir"]        # 1 to kMaxCameras, each with a table
struct Config {
  config::LoggingConfig log;
  std::string station_id;
  std::filesystem::path command_socket;
  std::filesystem::path time_socket;
  std::filesystem::path offload_folder;
  // How long a command waits for ics-timingd's first report before it arms
  // the cameras without one.
  Duration time_wait{};
  // In the order the cameras setting lists them.
  std::vector<CameraConfig> cameras;
};

// Reads the [log], [camd] and [camera.ID] tables under root.
[[nodiscard]] Config read_config(config::Reader& root);

// Whether name can name a folder of its own: 1 to 64 letters, digits, '.',
// '_' or '-', and not "." or "..". Run and camera IDs must.
[[nodiscard]] bool folder_name(std::string_view name) noexcept;

// What the TOML schema cannot check: each camera ID names a folder of its
// own, and no two are the same. kInvalidArgument, with reason saying why.
[[nodiscard]] Status validate(const Config& config, std::string& reason);

}  // namespace ics::camd
