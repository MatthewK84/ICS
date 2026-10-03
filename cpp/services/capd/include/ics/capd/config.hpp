#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ics/capture/capture.hpp"
#include "ics/capture/rotating_writer.hpp"
#include "ics/config/logging_config.hpp"
#include "ics/config/reader.hpp"

namespace ics::capd {

// Where ics-capd asks ptp4l for TAI - UTC, which converts adapter time
// stamps to UTC: the [ptp] table, required with adapter time stamps and
// refused with host ones.
struct PtpConfig {
  std::filesystem::path ptp4l_socket;
  std::filesystem::path client_socket;
  std::uint8_t ptp_domain = 0;
};

// The settings of ics-capd (ICS-020), from its TOML file:
//
//   [log]
//   level = "info"
//   service = "ics-capd"
//
//   [capture]
//   interfaces = ["tap0", "tap1"]           # 1 to 4 TAP ports
//   folder = "/var/lib/ics/capture"          # where the pcap files go
//   snaplen = 65535                          # 64 to 262144 bytes kept per packet
//   buffer_bytes = 67_108_864                # 1 MiB to 1 GiB of kernel ring per interface
//   timestamps = "adapter"                   # adapter or host
//   rotate_interval_ns = 3_600_000_000_000   # 1 s to 1 day
//   rotate_bytes = 4_000_000_000             # 1 MiB to 1 TiB
//
//   [ptp]                                    # only with adapter time stamps
//   ptp4l_socket = "/var/run/ptp4l-ro"
//   client_socket = "/run/ics-capd/ptp4l-client"
//   ptp_domain = 0
struct Config {
  config::LoggingConfig log;
  std::vector<std::string> interfaces;
  std::filesystem::path folder;
  std::uint32_t snaplen = 0;
  std::uint32_t buffer_bytes = 0;
  capture::TimestampSource timestamps = capture::TimestampSource::kHost;
  capture::RotationLimits rotation;
  std::optional<PtpConfig> ptp;
};

// The most interfaces one ics-capd captures: a station's TAP ports.
inline constexpr std::size_t kMaxInterfaces = 4;

// Reads the [log], [capture] and, for adapter time stamps, [ptp] tables
// under root.
[[nodiscard]] Config read_config(config::Reader& root);

// Whether name is one Linux accepts for an interface: 1 to 15 characters,
// not "." or "..", with no '/', ':' or white space. Capture file names start
// with it, so it must not name another folder.
[[nodiscard]] bool valid_interface_name(std::string_view name) noexcept;

}  // namespace ics::capd
