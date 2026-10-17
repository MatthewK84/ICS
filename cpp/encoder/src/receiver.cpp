#include "ics/encoder/receiver.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "ics/common/error.hpp"
#include "ics/tagger/datagram.hpp"
#include "ics/tagger/sampler.hpp"
#include "ics/timing/unix_socket.hpp"
#include "status.hpp"

namespace ics::encoder {

using detail::status_of;

DatagramReceiver::DatagramReceiver(timing::Fd socket, const std::uint16_t port) noexcept
    : socket_(std::move(socket)), port_(port) {}

Result<DatagramReceiver> DatagramReceiver::make(const std::string& address, const std::uint16_t port) noexcept {
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(port);
  if (::inet_pton(AF_INET, address.c_str(), &local.sin_addr) != 1) {
    return fail(Error::kInvalidArgument);
  }
  timing::Fd socket(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  sockaddr_in bound{};
  socklen_t size = sizeof(bound);
  // bind and getsockname take an AF_INET address as a sockaddr.
  return status_of(socket.valid(), Error::kUnavailable)
      .and_then([&socket, &local] {
        return status_of(::bind(socket.get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == 0,
                         Error::kUnavailable);
      })
      .and_then([&socket, &bound, &size] {
        return status_of(::getsockname(socket.get(), reinterpret_cast<sockaddr*>(&bound), &size) == 0,
                         Error::kUnavailable);
      })
      .map([&socket, &bound] { return DatagramReceiver(std::move(socket), ntohs(bound.sin_port)); });
}

Result<tagger::Batch> DatagramReceiver::receive(const std::chrono::milliseconds timeout) noexcept {
  pollfd ready{.fd = socket_.get(), .events = POLLIN, .revents = 0};
  if (::poll(&ready, 1, static_cast<int>(std::max<std::int64_t>(timeout.count(), 0))) != 1) {
    return fail(Error::kEmpty);
  }
  // MSG_TRUNC returns a datagram's whole length, so one too long is seen.
  const ssize_t got = ::recv(socket_.get(), buffer_.data(), buffer_.size(), MSG_DONTWAIT | MSG_TRUNC);
  return status_of(got >= 0, Error::kUnavailable).and_then([this, got]() -> Result<tagger::Batch> {
    ++stats_.datagrams;
    const auto length = static_cast<std::size_t>(got);
    const Result<tagger::Batch> batch =
        length == buffer_.size() ? tagger::decode(buffer_) : fail(Error::kMalformed);
    stats_.malformed += batch ? 0U : 1U;
    return batch;
  });
}

}  // namespace ics::encoder
