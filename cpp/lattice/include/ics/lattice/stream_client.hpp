#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/lattice/event.hpp"

namespace ics::lattice {

// The Lattice entity stream (ICS-023): POST <url>/api/v1/entities/stream,
// read as server-sent events with libcurl. It asks for every entity that
// exists and then every change, with a heartbeat every heartbeat_interval, and
// hands each entity event it decodes to an EventSink.
//
// - Authorization: Bearer <token>, and Anduril-Sandbox-Authorization: Bearer
//   <sandbox_token> for a Lattice Sandbox. The client only ever reads the
//   stream; the token's read-only scope is set in Lattice.
// - HTTPS with the server's certificate and name verified, against ca_file
//   (libcurl's default when empty). Plain HTTP only to this host, for tests
//   or a local proxy. Redirects are not followed. libcurl takes a proxy from
//   the https_proxy environment variable.
// - When the connection fails or ends, the server answers 408, 429 or 5xx,
//   or the stream is silent for three heartbeat intervals, it connects again
//   after a pause that doubles from first_backoff to max_backoff, and goes
//   back to first_backoff once a connection delivers an event. Each new
//   connection sends every existing entity again.
// - Any other 4xx answer, such as 401 or 403 for a token Lattice refuses,
//   ends the run with an error rather than retrying a request that cannot
//   succeed.

struct ClientSettings {
  // Lattice's base URL, such as https://lattice.example.com.
  std::string url;
  std::string token;
  // Empty: not a Lattice Sandbox.
  std::string sandbox_token;
  std::string ca_file = "/etc/ssl/certs/ca-certificates.crt";
  // The entity components to ask for, such as "location"; empty asks for all.
  std::vector<std::string> components{};
  std::chrono::seconds heartbeat_interval{5};
  std::chrono::seconds connect_timeout{10};
  Duration first_backoff = std::chrono::seconds(1);
  Duration max_backoff = std::chrono::seconds(30);
};

// Takes each entity event the client reads, with the time it arrived.
class EventSink {
 public:
  virtual void accept(const Event& event, UtcTime received) = 0;

 protected:
  EventSink() = default;
  ~EventSink() = default;
  EventSink(const EventSink&) = default;
  EventSink& operator=(const EventSink&) = default;
  EventSink(EventSink&&) = default;
  EventSink& operator=(EventSink&&) = default;
};

struct ClientStats {
  // Connections tried.
  std::uint64_t connections = 0;
  // Entity events handed to the sink.
  std::uint64_t events = 0;
  // Event data decode_event refused.
  std::uint64_t malformed = 0;
  // Events dropped for passing SseReader's limit.
  std::uint64_t oversized = 0;
  // The last connection's HTTP status, and how it ended in libcurl's words.
  std::int64_t last_http_status = 0;
  std::string last_error;
};

class StreamClient {
 public:
  // Fails with Error::kInvalidArgument for a URL that is not HTTPS or HTTP to
  // this host, a token that is not valid_token(), a component name that is
  // not letters and underscores, a heartbeat interval or connect timeout
  // under a second, or a backoff that is not positive and increasing.
  [[nodiscard]] static Result<StreamClient> make(ClientSettings settings);

  ~StreamClient();
  StreamClient(StreamClient&& other) noexcept;
  StreamClient& operator=(StreamClient&& other) noexcept;
  StreamClient(const StreamClient&) = delete;
  StreamClient& operator=(const StreamClient&) = delete;

  // Streams events to the sink until stop is set, then returns. Fails with
  // Error::kUnavailable when the server refuses the request; stats() says how.
  [[nodiscard]] Status run(EventSink& sink, const std::atomic<bool>& stop);

  [[nodiscard]] const ClientStats& stats() const noexcept { return stats_; }

 private:
  struct Connection;
  enum class Outcome { kDelivered, kFailed, kRefused };

  StreamClient(ClientSettings settings, std::unique_ptr<Connection> connection) noexcept;
  [[nodiscard]] Outcome attempt(EventSink& sink, const std::atomic<bool>& stop);

  ClientSettings settings_;
  std::unique_ptr<Connection> connection_;
  ClientStats stats_;
};

}  // namespace ics::lattice
