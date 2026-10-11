#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "ics/common/error.hpp"
#include "ics/mount/limits.hpp"
#include "ics/mount/pwi4_status.hpp"

namespace ics::mount {

// When a request must be done by, retries included.
using Deadline = std::chrono::steady_clock::time_point;

// How a request is retried. Every command the client sends is idempotent,
// so sending one again is safe.
struct RetryPolicy {
  // Attempts at each request, the first included.
  std::uint32_t attempts = 3;
  // The wait before the second attempt; each later wait doubles it. A wait
  // that would pass the deadline is not taken.
  std::chrono::milliseconds first_backoff{5};
};

// Where PWI4 listens, and how long to wait for it.
struct Pwi4Settings {
  // PWI4's host, a name or an address, and its HTTP port (PWI4's default is
  // 8220).
  std::string host = "127.0.0.1";
  std::uint16_t port = 8220;
  // The longest one attempt may take, cut short by the request's deadline.
  std::chrono::milliseconds timeout{10};
  RetryPolicy retry;
  // Every goto and rate is held to these before it is sent.
  MountLimits limits;
};

// What the client has sent, and how it went.
struct ClientStats {
  std::uint64_t requests = 0;
  std::uint64_t attempts = 0;
  // Requests that failed every attempt.
  std::uint64_t failures = 0;
  // How the last failed attempt failed.
  std::string last_error;
};

// PlaneWave Interface 4's HTTP interface for a station mount (ICS-033), as
// PlaneWave documents it: GET requests with their parameters in the query,
// each answered with keyword=value status text. It works in ICS units and
// holds every goto and rate to the limits before sending it. Each request is
// retried under the retry policy, and none outlasts its deadline. One
// connection is kept open across requests. Not safe to use from two threads
// at once.
class Pwi4Client {
 public:
  // Error::kInvalidArgument for no host, port 0, no attempts or no timeout.
  [[nodiscard]] static Result<Pwi4Client> make(Pwi4Settings settings);

  ~Pwi4Client();
  Pwi4Client(const Pwi4Client&) = delete;
  Pwi4Client& operator=(const Pwi4Client&) = delete;
  Pwi4Client(Pwi4Client&& other) noexcept;
  Pwi4Client& operator=(Pwi4Client&& other) noexcept;

  // Every request fails with Error::kUnavailable when every attempt fails:
  // no answer within the timeout, an HTTP status other than 200, or an
  // answer over kMaxStatusBytes.

  // /status. Also Error::kMalformed for status text parse_status refuses.
  [[nodiscard]] Result<MountStatus> status(Deadline deadline);
  // /mount/connect.
  [[nodiscard]] Status connect(Deadline deadline);
  // /mount/enable for one axis.
  [[nodiscard]] Status enable(Axis axis, Deadline deadline);
  // /mount/goto_alt_az, after asking for the status and checking the target
  // with check_goto: Error::kOutOfRange, sending nothing, when it fails.
  [[nodiscard]] Status goto_alt_az(const AltAz& target, Deadline deadline);
  // /mount/offset with each axis's set_rate_arcsec_per_sec: the demand, held
  // to the limits by limit_rates against status, which should be recent.
  // Returns what was sent.
  [[nodiscard]] Result<AppliedRates> set_rates(const AxisRates& demand, const MountStatus& status,
                                               Deadline deadline);
  // /mount/offset with each axis's stop_rate.
  [[nodiscard]] Status stop_rates(Deadline deadline);
  // /mount/stop.
  [[nodiscard]] Status stop(Deadline deadline);

  [[nodiscard]] const ClientStats& stats() const noexcept { return stats_; }

 private:
  struct Connection;

  Pwi4Client(Pwi4Settings settings, std::unique_ptr<Connection> connection) noexcept;

  // GETs path, retrying under the policy: the answer's body.
  [[nodiscard]] Result<std::string> request(const std::string& path, Deadline deadline);
  // One attempt: whether it got a 200 answer, which body then holds.
  [[nodiscard]] bool attempt(Deadline deadline, std::string& body);

  Pwi4Settings settings_;
  std::string base_url_;
  std::unique_ptr<Connection> connection_;
  ClientStats stats_;
};

}  // namespace ics::mount
