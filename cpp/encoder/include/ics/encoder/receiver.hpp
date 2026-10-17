#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "ics/common/error.hpp"
#include "ics/tagger/datagram.hpp"
#include "ics/tagger/sampler.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::encoder {

// What a receiver has read.
struct ReceiverStats {
  // Datagrams read, and those among them that were not a tagger batch:
  // another size, magic or version, or a CRC that did not match.
  std::uint64_t datagrams = 0;
  std::uint64_t malformed = 0;
};

// A UDP socket that the encoder time-tagger (ICS-032) sends its batches to,
// 100 datagrams a second. In operation it is ics-mountd's (ICS-041), bound
// to the station network's address and the port the tagger is set to send
// to; on the bench it is bound to the loopback address.
class DatagramReceiver {
 public:
  // A socket bound to address, IPv4 such as "0.0.0.0" for every interface,
  // and port; port 0 takes a free one. kInvalidArgument for an address that
  // is not IPv4; kUnavailable when the socket cannot be made or bound, as
  // when another socket holds the port.
  [[nodiscard]] static Result<DatagramReceiver> make(const std::string& address, std::uint16_t port) noexcept;

  // The port bound: the one asked for, or the one taken for port 0.
  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

  // The next datagram, if one comes within timeout, decoded. kEmpty when
  // none came; kMalformed, and counted, for one that is not a tagger batch;
  // kUnavailable when the socket cannot be read.
  [[nodiscard]] Result<tagger::Batch> receive(std::chrono::milliseconds timeout) noexcept;

  [[nodiscard]] const ReceiverStats& stats() const noexcept { return stats_; }

 private:
  DatagramReceiver(timing::Fd socket, std::uint16_t port) noexcept;

  timing::Fd socket_;
  std::uint16_t port_ = 0;
  ReceiverStats stats_;
  std::array<std::byte, tagger::kDatagramBytes> buffer_{};
};

}  // namespace ics::encoder
