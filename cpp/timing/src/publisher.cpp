#include "ics/timing/publisher.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <system_error>
#include <utility>

#include <sys/socket.h>
#include <sys/types.h>

#include "ics/common/check.hpp"

namespace ics::timing {

Publisher::Publisher(Fd listener, std::filesystem::path path) noexcept
    : listener_(std::move(listener)), path_(std::move(path)) {}

Publisher::~Publisher() {
  if (listener_.valid()) {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }
}

Result<Publisher> Publisher::open(const std::filesystem::path& path) noexcept {
  Result<Fd> listener = bound_socket(path, SocketRole::kListener);
  if (!listener) {
    return fail(listener.error());
  }
  return Publisher(std::move(*listener), path);
}

void Publisher::accept_waiting() noexcept {
  while (!subscribers_.full()) {
    Fd subscriber(::accept4(listener_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
    if (!subscriber.valid()) {
      return;
    }
    // Room was checked above, so this cannot fail.
    static_cast<void>(ics::check(subscribers_.push_back(std::move(subscriber)).has_value()));
  }
}

void Publisher::publish(const std::span<const std::byte> message) noexcept {
  accept_waiting();
  std::span<Fd> subscribers = subscribers_.span();
  std::size_t kept = 0;
  for (Fd& subscriber : subscribers) {
    const ssize_t sent = ::send(subscriber.get(), message.data(), message.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
    if (sent == static_cast<ssize_t>(message.size())) {
      std::swap(subscribers[kept], subscriber);
      ++kept;
    }
  }
  // The dropped subscribers are now at the end; popping them closes them.
  while (subscribers_.size() > kept) {
    static_cast<void>(subscribers_.pop_back());
  }
}

}  // namespace ics::timing
