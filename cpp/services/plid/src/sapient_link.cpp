#include "ics/plid/sapient_link.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include "ics/common/check.hpp"

namespace ics::plid {

SapientLink::SapientLink(const sockaddr_in address, sapient::Adapter adapter)
    : address_(address), adapter_(std::move(adapter)), buffer_(kReadBytes) {}

Result<SapientLink> SapientLink::make(const std::string& address, const std::uint16_t port, sapient::Adapter adapter,
                                      std::string& reason) {
  sockaddr_in parsed{};
  parsed.sin_family = AF_INET;
  parsed.sin_port = htons(port);
  if (::inet_pton(AF_INET, address.c_str(), &parsed.sin_addr) != 1) {
    reason = "the SAPIENT address must be IPv4, such as 10.0.0.5: " + address;
    return fail(Error::kInvalidArgument);
  }
  return SapientLink(parsed, std::move(adapter));
}

pollfd SapientLink::descriptor() const noexcept {
  const short events = state_ == State::kConnecting ? POLLOUT : POLLIN;
  return pollfd{socket_.get(), events, 0};
}

void SapientLink::step(const UtcTime now, std::vector<v1::PliRecord>& out, const logging::Logger& logger) {
  if (state_ == State::kWaiting && now >= retry_at_) {
    connect(now, logger);
  } else if (state_ == State::kConnecting) {
    finish_connect(now, logger);
  } else if (state_ == State::kConnected) {
    read(now, out, logger);
  }
}

// Starts a connection; finish_connect learns how it went.
void SapientLink::connect(const UtcTime now, const logging::Logger& logger) {
  static_cast<void>(ics::check(state_ == State::kWaiting));
  ++counts_.connections;
  socket_ = timing::Fd(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
  if (!socket_.valid()) {
    drop(now, logger, "no socket");
    return;
  }
  // A non-blocking connect to another host returns at once with EINPROGRESS;
  // either way, finish_connect sees the outcome.
  static_cast<void>(::connect(socket_.get(), reinterpret_cast<const sockaddr*>(&address_), sizeof(address_)));
  state_ = State::kConnecting;
}

// A connection attempt is over once the socket can be written: it connected
// if it has a peer.
void SapientLink::finish_connect(const UtcTime now, const logging::Logger& logger) {
  static_cast<void>(ics::check(state_ == State::kConnecting));
  pollfd ready = descriptor();
  if (::poll(&ready, 1, 0) != 1) {
    return;
  }
  sockaddr_in peer{};
  socklen_t size = sizeof(peer);
  if (::getpeername(socket_.get(), reinterpret_cast<sockaddr*>(&peer), &size) != 0) {
    drop(now, logger, "connect failed");
    return;
  }
  state_ = State::kConnected;
  backoff_ = kFirstBackoff;
  logger.info("sapient_connected", {{"connections", static_cast<std::int64_t>(counts_.connections)}});
}

void SapientLink::read(const UtcTime now, std::vector<v1::PliRecord>& out, const logging::Logger& logger) {
  static_cast<void>(ics::check(state_ == State::kConnected));
  for (int count = 0; count < kMaxReads && state_ == State::kConnected; ++count) {
    const ssize_t got = ::recv(socket_.get(), buffer_.data(), buffer_.size(), MSG_DONTWAIT);
    const int error = errno;
    if (got > 0) {
      take(std::span(buffer_).first(static_cast<std::size_t>(got)), now, out, logger);
    } else if (got < 0 && error == EAGAIN) {
      return;
    } else {
      drop(now, logger, got == 0 ? "closed by the middleware" : "read failed");
    }
  }
}

void SapientLink::take(const std::span<const std::byte> bytes, const UtcTime now, std::vector<v1::PliRecord>& out,
                       const logging::Logger& logger) {
  static_cast<void>(ics::check(bytes.size() <= buffer_.size()));
  for (const sapient::Message& message : reader_.feed(bytes)) {
    ++counts_.messages;
    std::optional<v1::PliRecord> record = adapter_.receive(message, now);
    if (record) {
      ++counts_.records;
      out.push_back(std::move(*record));
    }
  }
  if (reader_.broken()) {
    drop(now, logger, "a message broke the framing");
  }
}

void SapientLink::drop(const UtcTime now, const logging::Logger& logger, const std::string_view why) {
  socket_ = timing::Fd();
  reader_ = sapient::StreamReader();
  state_ = State::kWaiting;
  retry_at_ = now + backoff_;
  logger.warn("sapient_disconnected", {{"reason", std::string(why)}, {"retry_in_ns", backoff_.count()}});
  backoff_ = std::min(backoff_ * 2, kMaxBackoff);
}

}  // namespace ics::plid
