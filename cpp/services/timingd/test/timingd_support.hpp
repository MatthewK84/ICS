#pragma once

// Helpers for the ics-timingd tests (ICS-019).

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "ics/common/units.hpp"
#include "ics/logging/json_line.hpp"
#include "ics/timing/unix_socket.hpp"
#include "ics/timingd/config.hpp"
#include "ics/v1/time_quality.pb.h"
#include "support.hpp"

namespace ics::timingd::testing {

// The time every test logger stamps its lines with.
inline UtcTime fixed_clock() noexcept { return utc_from_ns(1'790'000'000'000'000'000); }

// A config with its sockets in dir and a 50 ms poll interval.
inline Config test_config(const timing::testing::TempDir& dir) {
  Config config;
  config.log = {logging::Level::kDebug, "ics-timingd"};
  config.station_id = "station-1";
  config.ptp4l_socket = dir / "ptp4l-ro";
  config.client_socket = dir / "client";
  config.publish_socket = dir / "time-quality";
  config.camera_offsets_file = dir / "offsets.binpb";
  config.poll_interval = std::chrono::milliseconds(50);
  config.model = {std::chrono::microseconds(1), 50.0};
  return config;
}

// A subscriber to the reports published at path.
class Subscriber {
 public:
  explicit Subscriber(const std::filesystem::path& path)
      : socket_(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)), connected_(connect(socket_, path)) {}

  // The next report, waiting up to a second for it; none if not connected.
  [[nodiscard]] std::optional<v1::TimeQuality> next() const {
    pollfd ready{socket_.get(), POLLIN, 0};
    std::array<std::byte, 512> buffer{};
    const bool waiting = connected_ && ::poll(&ready, 1, 1000) > 0;
    const ssize_t size = waiting ? ::recv(socket_.get(), buffer.data(), buffer.size(), 0) : -1;
    v1::TimeQuality report;
    if (size < 0 || !report.ParseFromArray(buffer.data(), static_cast<int>(size))) {
      return std::nullopt;
    }
    return report;
  }

 private:
  static bool connect(const timing::Fd& socket, const std::filesystem::path& path) {
    const sockaddr_un address = timing::unix_address(path).value();
    return ::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
  }

  timing::Fd socket_;
  bool connected_ = false;
};

}  // namespace ics::timingd::testing
