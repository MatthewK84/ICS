#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ics/capture/live_capture.hpp"
#include "ics/capture/rotating_writer.hpp"
#include "ics/config/logging_config.hpp"
#include "ics/config/reader.hpp"

namespace ics::capd {

// The most TAP ports one ics-capd captures.
inline constexpr std::size_t kMaxInterfaces = 4;

// The settings of ics-capd (ICS-020), from /etc/ics/ics-capd.toml:
//
//   [log]
//   level = "info"
//   service = "ics-capd"
//
//   [capture]
//   interfaces = ["tap0"]                 # 1 to 4 TAP ports
//   output_dir = "/var/lib/ics/capture"   # must exist
//   snaplen = 65_535                      # bytes kept per packet, 64 to 262_144
//   ring_bytes = 67_108_864               # kernel ring per port, 1 MiB to 1 GiB
//   timestamps = "adapter"                # "adapter" (the NIC's clock) or "host"
//   rotate_interval_ns = 3_600_000_000_000  # 1 s to 24 h
//   rotate_bytes = 1_073_741_824          # 1 MiB to 1 TiB
//   ptp4l_socket = "/var/run/ptp4l-ro"    # with "adapter": TAI-UTC from ptp4l
//   client_socket = "/run/ics-capd/ptp4l-client"
//   ptp_domain = 0
struct Config {
  config::LoggingConfig log;
  std::vector<std::string> interfaces;
  std::filesystem::path output_dir;
  std::uint32_t snaplen = 0;
  std::uint32_t ring_bytes = 0;
  capture::TimestampSource timestamps = capture::TimestampSource::kHost;
  capture::RotationLimits rotation;
  std::filesystem::path ptp4l_socket;
  std::filesystem::path client_socket;
  std::uint8_t ptp_domain = 0;
};

// Reads the [log] and [capture] tables under root.
[[nodiscard]] Config read_config(config::Reader& root);

}  // namespace ics::capd
