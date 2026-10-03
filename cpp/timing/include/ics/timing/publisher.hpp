#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

#include "ics/common/error.hpp"
#include "ics/common/static_vector.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::timing {

// Sends each message to every process subscribed on a local socket
// (ICS-019). ics-timingd publishes ics.v1.TimeQuality this way until gRPC
// joins the toolchain: a subscriber connects to the socket and reads one
// serialized message per read, the shape of a gRPC server stream.
//
// The socket is SOCK_SEQPACKET, so each message arrives whole. A subscriber
// that cannot take a message at once is dropped, so a stalled one cannot
// hold up the others; it reconnects to resume.
class Publisher {
 public:
  static constexpr std::size_t kMaxSubscribers = 16;

  // Listens at path: a file left there is removed first, and the socket
  // again when the publisher is destroyed.
  [[nodiscard]] static Result<Publisher> open(const std::filesystem::path& path) noexcept;

  ~Publisher();
  Publisher(Publisher&& other) noexcept = default;
  Publisher& operator=(Publisher&& other) noexcept = default;
  Publisher(const Publisher&) = delete;
  Publisher& operator=(const Publisher&) = delete;

  // Accepts the subscribers waiting, up to kMaxSubscribers, then sends message
  // to each. Allocates nothing.
  void publish(std::span<const std::byte> message) noexcept;

  [[nodiscard]] std::size_t subscribers() const noexcept { return subscribers_.size(); }

 private:
  Publisher(Fd listener, std::filesystem::path path) noexcept;

  void accept_waiting() noexcept;

  Fd listener_;
  StaticVector<Fd, kMaxSubscribers> subscribers_;
  std::filesystem::path path_;
};

}  // namespace ics::timing
