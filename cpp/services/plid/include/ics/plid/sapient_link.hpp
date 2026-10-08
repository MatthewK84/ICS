#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <netinet/in.h>
#include <poll.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/logging/logger.hpp"
#include "ics/sapient/adapter.hpp"
#include "ics/sapient/stream.hpp"
#include "ics/timing/unix_socket.hpp"
#include "ics/v1/pli.pb.h"

namespace ics::plid {

struct SapientCounts {
  std::uint64_t connections = 0;
  std::uint64_t messages = 0;
  std::uint64_t records = 0;
};

// ics-plid's TCP connection to a SAPIENT middleware (ICS-030), read without
// ever blocking the service. It connects, reads the length-framed messages
// (ICS-024) into the SAPIENT adapter, and when the connection fails, closes or
// sends a stream that breaks its framing, connects again after a pause that
// doubles from kFirstBackoff to kMaxBackoff, and goes back to kFirstBackoff
// once connected. The kernel's own timeout ends a connection attempt that
// gets no answer.
class SapientLink {
 public:
  static constexpr Duration kFirstBackoff = std::chrono::seconds(1);
  static constexpr Duration kMaxBackoff = std::chrono::seconds(30);
  // Bytes read per call, and calls per step.
  static constexpr std::size_t kReadBytes = std::size_t{1} << 16U;
  static constexpr int kMaxReads = 16;

  // A link to address, an IPv4 address such as "10.0.0.5", and port; the
  // first attempt is at the first step. kInvalidArgument, with an explanation
  // in reason, for an address that is not IPv4.
  [[nodiscard]] static Result<SapientLink> make(const std::string& address, std::uint16_t port,
                                                sapient::Adapter adapter, std::string& reason);

  // What to poll: the socket, for writing while it connects and for reading
  // once connected; fd -1, which poll skips, while waiting to retry.
  [[nodiscard]] pollfd descriptor() const noexcept;

  // Moves the connection on at now, adding the record of each detection read
  // to out. Logs "sapient_connected", and warns "sapient_disconnected" with
  // why and when it will retry.
  void step(UtcTime now, std::vector<v1::PliRecord>& out, const logging::Logger& logger);

  [[nodiscard]] bool connected() const noexcept { return state_ == State::kConnected; }
  [[nodiscard]] const SapientCounts& counts() const noexcept { return counts_; }

 private:
  enum class State : std::uint8_t { kWaiting, kConnecting, kConnected };

  SapientLink(sockaddr_in address, sapient::Adapter adapter);
  void connect(UtcTime now, const logging::Logger& logger);
  void finish_connect(UtcTime now, const logging::Logger& logger);
  void read(UtcTime now, std::vector<v1::PliRecord>& out, const logging::Logger& logger);
  void take(std::span<const std::byte> bytes, UtcTime now, std::vector<v1::PliRecord>& out,
            const logging::Logger& logger);
  void drop(UtcTime now, const logging::Logger& logger, std::string_view why);

  sockaddr_in address_{};
  sapient::Adapter adapter_;
  sapient::StreamReader reader_;
  timing::Fd socket_;
  std::vector<std::byte> buffer_;
  State state_ = State::kWaiting;
  UtcTime retry_at_ = UtcTime::min();
  Duration backoff_ = kFirstBackoff;
  SapientCounts counts_;
};

}  // namespace ics::plid
