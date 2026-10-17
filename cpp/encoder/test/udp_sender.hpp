#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "ics/tagger/datagram.hpp"
#include "ics/tagger/sampler.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::encoder::testing {

// Sends datagrams to a port on the loopback address, as the tagger sends
// them to ics-mountd.
class UdpSender {
 public:
  explicit UdpSender(const std::uint16_t port) : socket_(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) {
    to_.sin_family = AF_INET;
    to_.sin_port = htons(port);
    to_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  }

  [[nodiscard]] bool send(const std::span<const std::byte> bytes) const {
    // sendto takes an AF_INET address as a sockaddr.
    const ssize_t sent =
        ::sendto(socket_.get(), bytes.data(), bytes.size(), 0, reinterpret_cast<const sockaddr*>(&to_), sizeof(to_));
    return sent == static_cast<ssize_t>(bytes.size());
  }

  [[nodiscard]] bool send(const tagger::Batch& batch) const {
    std::array<std::byte, tagger::kDatagramBytes> bytes{};
    return tagger::encode(batch, bytes) == bytes.size() && send(bytes);
  }

 private:
  timing::Fd socket_;
  sockaddr_in to_{};
};

}  // namespace ics::encoder::testing
