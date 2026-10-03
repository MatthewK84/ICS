#pragma once

// Helpers for the ics-capd tests (ICS-020).

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "ics/capd/config.hpp"
#include "ics/capture/live_capture.hpp"
#include "ics/common/units.hpp"
#include "ics/logging/json_line.hpp"
#include "ics/logging/logger.hpp"
#include "support.hpp"

namespace ics::capd::testing {

inline UtcTime fixed_clock() noexcept { return utc_from_ns(1'790'000'000'000'000'000); }

// A config capturing the loopback interface with host time stamps into dir,
// rotating every minute.
inline Config test_config(const timing::testing::TempDir& dir) {
  Config config;
  config.log = {logging::Level::kDebug, "ics-capd"};
  config.interfaces = {"lo"};
  config.output_dir = dir / "";
  config.snaplen = 256;
  config.ring_bytes = 1U << 20U;
  config.timestamps = capture::TimestampSource::kHost;
  config.rotation = {std::chrono::seconds(60), 1U << 20U};
  config.ptp4l_socket = dir / "ptp4l-ro";
  config.client_socket = dir / "client";
  return config;
}

// A logger writing to a string.
struct Logged {
  std::ostringstream out;
  logging::Logger logger = logging::Logger::to_stream("ics-capd", logging::Level::kDebug, out, &fixed_clock);
};

// The pcap files in dir.
inline std::vector<std::filesystem::path> pcap_files(const timing::testing::TempDir& dir) {
  std::vector<std::filesystem::path> out;
  for (const auto& entry : std::filesystem::directory_iterator(dir / "")) {
    if (entry.path().extension() == ".pcap") {
      out.push_back(entry.path());
    }
  }
  return out;
}

}  // namespace ics::capd::testing
