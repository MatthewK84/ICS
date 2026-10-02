#include "ics/timing/unix_socket.hpp"

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ics::timing {
namespace {

// Room for this many connections waiting to be accepted.
constexpr int kBacklog = 16;

}  // namespace

Fd::Fd(const int fd) noexcept : fd_(fd) {}

Fd::~Fd() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

Fd::Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

Fd& Fd::operator=(Fd&& other) noexcept {
  // The old descriptor ends up in taken, which closes it; safe for self-move.
  Fd taken(std::move(other));
  std::swap(fd_, taken.fd_);
  return *this;
}

Result<sockaddr_un> unix_address(const std::filesystem::path& path) noexcept {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  const std::string& name = path.native();
  const std::span<char> room(address.sun_path);
  // The last byte stays zero, so the name is terminated.
  if (name.empty() || name.size() >= room.size()) {
    return fail(Error::kInvalidArgument);
  }
  std::ranges::copy(name, room.begin());
  return address;
}

Result<Fd> bound_socket(const std::filesystem::path& path, const SocketRole role) noexcept {
  const Result<sockaddr_un> address = unix_address(path);
  if (!address) {
    return fail(address.error());
  }
  const bool listener = role == SocketRole::kListener;
  Fd socket(::socket(AF_UNIX, (listener ? SOCK_SEQPACKET : SOCK_DGRAM) | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
  if (!socket.valid()) {
    return fail(Error::kUnavailable);
  }
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  // bind takes an AF_UNIX address as a sockaddr.
  const int bound = ::bind(socket.get(), reinterpret_cast<const sockaddr*>(&*address), sizeof(sockaddr_un));
  const int ready = bound == 0 && listener ? ::listen(socket.get(), kBacklog) : bound;
  if (ready != 0) {
    return fail(Error::kUnavailable);
  }
  return socket;
}

}  // namespace ics::timing
