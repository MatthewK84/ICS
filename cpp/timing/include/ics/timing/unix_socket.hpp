#pragma once

#include <filesystem>

#include <sys/un.h>

#include "ics/common/error.hpp"

namespace ics::timing {

// A file descriptor, closed when this is destroyed (ICS-019).
class Fd {
 public:
  Fd() noexcept = default;
  explicit Fd(int fd) noexcept;
  ~Fd();
  Fd(Fd&& other) noexcept;
  Fd& operator=(Fd&& other) noexcept;
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

 private:
  int fd_ = -1;
};

// The address of the Unix socket at path. kInvalidArgument when the path is
// empty or longer than a socket address holds (107 bytes on Linux).
[[nodiscard]] Result<sockaddr_un> unix_address(const std::filesystem::path& path) noexcept;

// What a bound socket is for.
enum class SocketRole : bool {
  kDatagram = false,  // A datagram socket, as ptp4l's management socket is.
  kListener = true,   // A SOCK_SEQPACKET socket listening for connections.
};

// A non-blocking Unix socket bound to path. A file already at path, such as
// a socket left by an earlier run, is removed first. kInvalidArgument for a
// path unix_address rejects; kUnavailable when the socket cannot be made or
// bound, for example in a folder that does not exist.
[[nodiscard]] Result<Fd> bound_socket(const std::filesystem::path& path, SocketRole role) noexcept;

}  // namespace ics::timing
