// scpi_server.h: an emulated 577 on a local TCP port, for the link's tests
// (ICS-040). It serves one connection, a line at a time, in the manner set:
// answering as the 577 does, saying nothing, hanging up, or rambling on
// without ever ending its line.
#ifndef ICS_TRIGGER_TEST_SCPI_SERVER_H
#define ICS_TRIGGER_TEST_SCPI_SERVER_H

#include <arpa/inet.h>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <gtest/gtest.h>

#include <emulated_577.h>
#include <ics/common/universal_types.h>

namespace ics::trigger_test
{

enum class Manner : uint8
{
  answering,
  silent,
  hanging_up,
  rambling
};

inline constexpr int32 poll_ms = 5;
inline constexpr std::size_t ramble_length = 400U;
inline constexpr std::size_t read_length = 256U;

// A TCP socket bound to a free local port, and that port.
struct Bound
{
  int32 socket;
  uint16 port;
};

inline Bound bind_local()
{
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  sockaddr general{};
  static_cast<void>(std::memcpy(&general, &address, sizeof(general)));
  const int32 socket = ::socket(AF_INET, SOCK_STREAM, 0);
  EXPECT_EQ(::bind(socket, &general, static_cast<socklen_t>(sizeof(general))), 0);
  socklen_t size = static_cast<socklen_t>(sizeof(general));
  static_cast<void>(::getsockname(socket, &general, &size));
  static_cast<void>(std::memcpy(&address, &general, sizeof(address)));
  return Bound{socket, ntohs(address.sin_port)};
}

// A local port nothing listens on.
inline uint16 closed_port()
{
  const Bound bound = bind_local();
  static_cast<void>(::close(bound.socket));
  return bound.port;
}

class Scpi_server
{
public:
  Scpi_server()
      : bound_(bind_local())
  {
    static_cast<void>(::listen(bound_.socket, 1));
    thread_ = std::thread(
      [this]()
      {
        serve();
      });
  }

  Scpi_server(const Scpi_server&) = delete;
  Scpi_server& operator=(const Scpi_server&) = delete;
  Scpi_server(Scpi_server&&) = delete;
  Scpi_server& operator=(Scpi_server&&) = delete;

  ~Scpi_server()
  {
    stopping_.store(true);
    thread_.join();
    static_cast<void>(::close(bound_.socket));
  }

  uint16 port() const
  {
    return bound_.port;
  }

  // The commands the 577 has taken.
  std::vector<std::string> commands()
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    return instrument_.commands;
  }

  std::atomic<Manner> manner{Manner::answering};

private:
  // Whether a socket has something to read, waiting a little.
  static bool readable(const int32 socket)
  {
    pollfd ready{socket, POLLIN, 0};
    return ::poll(&ready, 1, poll_ms) == 1;
  }

  // Answers one line in the manner set; false to hang up.
  bool reply_to(const int32 client, const std::string& line)
  {
    const Manner now = manner.load();
    std::string reply;
    if (now == Manner::answering)
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      reply = instrument_.answer(line) + "\r\n";
    }
    else if (now == Manner::rambling)
    {
      reply = std::string(ramble_length, 'x');
    }
    else
    {
      // Silent, or hanging up: nothing is said.
    }
    static_cast<void>(::send(client, reply.data(), reply.size(), MSG_NOSIGNAL));
    return now != Manner::hanging_up;
  }

  // Serves a client until it hangs up or the server stops.
  void converse(const int32 client)
  {
    std::string pending;
    std::string chunk(read_length, '\0');
    bool talking = true;
    while (talking && (!stopping_.load()))
    {
      ssize_t got = 1;
      if (readable(client))
      {
        got = ::recv(client, chunk.data(), chunk.size(), 0);
        static_cast<void>(pending.append(chunk, 0U, static_cast<std::size_t>((got > 0) ? got : 0)));
      }
      talking = got > 0;
      const std::size_t end = pending.find("\r\n");
      if (talking && (end != std::string::npos))
      {
        talking = reply_to(client, pending.substr(0U, end));
        pending.clear();
      }
    }
  }

  void serve()
  {
    while (!stopping_.load())
    {
      if (readable(bound_.socket))
      {
        const int32 client = ::accept(bound_.socket, nullptr, nullptr);
        converse(client);
        static_cast<void>(::close(client));
      }
    }
  }

  Bound bound_;
  Emulated_577 instrument_;
  std::mutex mutex_;
  std::atomic<bool> stopping_{false};
  std::thread thread_;
};

}  // namespace ics::trigger_test

#endif  // ICS_TRIGGER_TEST_SCPI_SERVER_H
