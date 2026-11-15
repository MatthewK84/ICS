// link.cpp: the TCP link to a 577 (ICS-040).
// JSF deviation D-6: ics::Result and ics::Duration come from .hpp headers.
#include <arpa/inet.h>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <ics/common/error.hpp>
#include <ics/common/units.hpp>
#include <ics/common/universal_types.h>
#include <ics/trigger/link.h>

namespace
{

// The longest reply taken: a 577's are a few tens of characters.
constexpr std::size_t max_reply = 256U;

}  // namespace

namespace ics::trigger
{

Tcp_link::Tcp_link(const int32 socket, const Duration timeout)
    : socket_(socket), timeout_(timeout)
{
}

Tcp_link::~Tcp_link()
{
  static_cast<void>(::close(socket_));
}

Result<std::unique_ptr<Tcp_link>> Tcp_link::open(const Link_settings& settings)
{
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(settings.port);
  Result<std::unique_ptr<Tcp_link>> link = fail(Error::kInvalidArgument);
  if (::inet_pton(AF_INET, settings.address.c_str(), &address.sin_addr) == 1)
  {
    // The socket calls take the general address type: the same bytes.
    sockaddr general{};
    static_cast<void>(std::memcpy(&general, &address, sizeof(general)));
    const int32 socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (::connect(socket, &general, static_cast<socklen_t>(sizeof(general))) == 0)
    {
      link = std::unique_ptr<Tcp_link>(new Tcp_link(socket, settings.timeout));
    }
    else
    {
      static_cast<void>(::close(socket));
      link = fail(Error::kUnavailable);
    }
  }
  return link;
}

Result<std::string> Tcp_link::read_line() const
{
  const int32 wait_ms = static_cast<int32>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout_).count());
  std::string text;
  std::string chunk(max_reply, '\0');
  Result<std::string> reply = fail(Error::kUnavailable);
  bool reading = true;
  while (reading && (text.size() < max_reply))
  {
    pollfd ready{socket_, POLLIN, 0};
    ssize_t got = 0;
    if (::poll(&ready, 1, wait_ms) == 1)
    {
      got = ::recv(socket_, chunk.data(), chunk.size(), 0);
    }
    // Nothing came in time, or the 577 closed the connection.
    reading = got > 0;
    if (reading)
    {
      static_cast<void>(text.append(chunk, 0U, static_cast<std::size_t>(got)));
      const std::size_t end = text.find('\n');
      if (end != std::string::npos)
      {
        reply = text.substr(0U, end);
        reading = false;
      }
    }
  }
  return reply;
}

Result<std::string> Tcp_link::ask(const std::string& command)
{
  const std::string line = command + "\r\n";
  // A command that could not be sent gets no reply, which is the failure.
  static_cast<void>(::send(socket_, line.data(), line.size(), MSG_NOSIGNAL));
  return read_line();
}

}  // namespace ics::trigger
