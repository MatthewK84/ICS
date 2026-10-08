#pragma once

// Helpers for the ics-plid tests (ICS-030).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "ics/common/units.hpp"
#include "ics/config/reader.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/logging/logger.hpp"
#include "ics/plid/config.hpp"
#include "ics/timing/unix_socket.hpp"
#include "ics/v1/pli_query.pb.h"

namespace ics::plid::testing {

inline constexpr std::int64_t kStartNs = 1'790'000'000'000'000'000;
inline const UtcTime kStart = utc_from_ns(kStartNs);

// The time every test logger stamps its lines with.
inline UtcTime fixed_clock() noexcept { return utc_from_ns(kStartNs); }

// A logger whose lines the test reads back. It is shared with the service's
// threads, so its stream is read only once they have stopped.
struct Logged {
  std::ostringstream out;
  logging::Logger logger =
      logging::Logger::to_stream("ics-plid", logging::Level::kDebug, out, &ics::plid::testing::fixed_clock);

  [[nodiscard]] bool has(const std::string_view text) const { return out.str().find(text) != std::string::npos; }
};

// The EGM96 grid where the ICS images install it.
inline const frames::Egm96& geoid() {
  static const frames::Egm96 grid = frames::Egm96::load(ICS_EGM96_PATH).value();
  return grid;
}

// The config in text, or its errors.
inline tl::expected<Config, config::Errors> read_text(const std::string_view text) {
  const auto table = config::parse(text);
  EXPECT_TRUE(table.has_value());
  return config::read(*table, &read_config);
}

// A config that replays the SITL crossing capture through the MAVLink feed,
// with the store in folder and the query socket in it.
inline Config replay_config(const std::filesystem::path& folder) {
  Config config;
  config.log = {logging::Level::kDebug, "ics-plid"};
  config.store_folder = folder;
  config.query_socket = folder / "query";
  config.rotate_interval = std::chrono::hours(1);
  config.sync_interval = std::chrono::seconds(1);
  config.feeds = {"mavlink"};
  config.range = {.latitude = Degrees(-35.363), .longitude = Degrees(149.165), .height = Meters(600.0)};
  config.files = {ICS_MAVLINK_FIXTURES "/crossing.pcap"};
  config.mavlink = MavlinkConfig{.roles = {"1=target", "2=interceptor"},
                                 .link_timeout = std::chrono::seconds(3),
                                 .max_age = std::chrono::seconds(1)};
  return config;
}

// Every response the query socket at path sends for request.
inline std::vector<v1::QueryPliResponse> query(const std::filesystem::path& path,
                                               const v1::QueryPliRequest& request) {
  const timing::Fd socket(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
  const sockaddr_un address = timing::unix_address(path).value();
  EXPECT_EQ(::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
  const std::string bytes = request.SerializeAsString();
  EXPECT_EQ(::send(socket.get(), bytes.data(), bytes.size(), MSG_NOSIGNAL), static_cast<ssize_t>(bytes.size()));
  std::vector<v1::QueryPliResponse> out;
  std::vector<char> buffer(std::size_t{1} << 20U);
  for (bool done = false; !done;) {
    const ssize_t got = ::recv(socket.get(), buffer.data(), buffer.size(), 0);
    v1::QueryPliResponse response;
    done = got <= 0 || !response.ParseFromArray(buffer.data(), static_cast<int>(got)) || response.done();
    if (got > 0) {
      out.push_back(response);
    }
  }
  return out;
}

// Every record or event the query socket returns over all time.
inline v1::QueryPliResponse everything(const std::filesystem::path& path, const v1::QueryPliRequest::Kind kind) {
  v1::QueryPliRequest request;
  request.set_kind(kind);
  request.set_start_utc_ns(INT64_MIN);
  request.set_end_utc_ns(INT64_MAX);
  v1::QueryPliResponse merged;
  for (const v1::QueryPliResponse& response : query(path, request)) {
    merged.MergeFrom(response);
  }
  return merged;
}

// A TCP listener on the loopback interface, at a port the kernel picks.
class TcpListener {
 public:
  explicit TcpListener(const int backlog = 4) : socket_(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    EXPECT_EQ(::bind(socket_.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
    EXPECT_EQ(::listen(socket_.get(), backlog), 0);
    EXPECT_EQ(::getsockname(socket_.get(), reinterpret_cast<sockaddr*>(&address), &size), 0);
    port_ = ntohs(address.sin_port);
  }

  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

  // The next connection, waiting for it.
  [[nodiscard]] timing::Fd accept() const { return timing::Fd(::accept4(socket_.get(), nullptr, nullptr, SOCK_CLOEXEC)); }

 private:
  timing::Fd socket_;
  std::uint16_t port_ = 0;
};

}  // namespace ics::plid::testing
