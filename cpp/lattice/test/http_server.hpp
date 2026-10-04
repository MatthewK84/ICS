#pragma once

// A loopback HTTP/1.1 server for the stream client's tests (ICS-023). Each
// connection gets the next scripted response: its head, then each piece of
// its body with a short pause between, and then the server closes the
// connection, or holds it open until the client closes it. It keeps every
// request it reads.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace ics::lattice::testing {

struct Response {
  // The status line and headers, each ending with CRLF.
  std::string head;
  std::vector<std::string> body{};
  bool hold = false;
};

// An SSE response holding these event lines.
inline Response stream(std::vector<std::string> body, const bool hold = false) {
  return Response{.head = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n",
                  .body = std::move(body),
                  .hold = hold};
}

// A response with no body and this status line, such as "401 Unauthorized".
inline Response status(const std::string& line) {
  return Response{.head = "HTTP/1.1 " + line + "\r\nContent-Length: 0\r\nConnection: close\r\n"};
}

class Socket {
 public:
  explicit Socket(const int fd) noexcept : fd_(fd) {}
  ~Socket() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  Socket(Socket&&) = delete;
  Socket& operator=(Socket&&) = delete;
  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  int fd_;
};

class TestServer {
 public:
  explicit TestServer(std::vector<Response> script)
      : listener_(::socket(AF_INET, SOCK_STREAM, 0)), script_(std::move(script)) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    EXPECT_EQ(::bind(listener_.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
    EXPECT_EQ(::listen(listener_.get(), 4), 0);
    EXPECT_EQ(::getsockname(listener_.get(), reinterpret_cast<sockaddr*>(&address), &size), 0);
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this] { serve(); });
  }
  ~TestServer() { thread_.join(); }
  TestServer(const TestServer&) = delete;
  TestServer& operator=(const TestServer&) = delete;
  TestServer(TestServer&&) = delete;
  TestServer& operator=(TestServer&&) = delete;

  [[nodiscard]] std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }

  // Each request's head and body, as read.
  [[nodiscard]] std::vector<std::string> requests() const {
    const std::scoped_lock lock(mutex_);
    return requests_;
  }

 private:
  static constexpr int kWaitMs = 10'000;
  static constexpr auto kPiecePause = std::chrono::milliseconds(20);

  void serve() {
    for (const Response& response : script_) {
      pollfd waiting{.fd = listener_.get(), .events = POLLIN, .revents = 0};
      if (::poll(&waiting, 1, kWaitMs) != 1) {
        return;
      }
      const Socket connection(::accept(listener_.get(), nullptr, nullptr));
      std::string request = read_request(connection.get());
      {
        const std::scoped_lock lock(mutex_);
        requests_.push_back(std::move(request));
      }
      answer(connection.get(), response);
    }
  }

  // The head, and the body Content-Length gives.
  static std::string read_request(const int fd) {
    std::string request;
    std::array<char, 4096> buffer{};
    std::size_t wanted = std::string::npos;
    while (request.size() < wanted) {
      pollfd readable{.fd = fd, .events = POLLIN, .revents = 0};
      const ssize_t got = ::poll(&readable, 1, kWaitMs) == 1 ? ::read(fd, buffer.data(), buffer.size()) : 0;
      if (got <= 0) {
        break;
      }
      request.append(buffer.data(), static_cast<std::size_t>(got));
      const std::size_t head_end = request.find("\r\n\r\n");
      const std::size_t length_at = request.find("Content-Length: ");
      if (head_end != std::string::npos && length_at != std::string::npos) {
        wanted = head_end + 4 + std::stoul(request.substr(length_at + 16));
      }
    }
    return request;
  }

  static void send_all(const int fd, const std::string& bytes) {
    EXPECT_EQ(::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL), static_cast<ssize_t>(bytes.size()));
  }

  static void answer(const int fd, const Response& response) {
    send_all(fd, response.head + "\r\n");
    for (const std::string& piece : response.body) {
      std::this_thread::sleep_for(kPiecePause);
      send_all(fd, piece);
    }
    if (response.hold) {
      // Until the client hangs up.
      std::array<char, 256> buffer{};
      pollfd readable{.fd = fd, .events = POLLIN, .revents = 0};
      while (::poll(&readable, 1, kWaitMs) == 1 && ::read(fd, buffer.data(), buffer.size()) > 0) {
      }
    }
  }

  Socket listener_;
  std::uint16_t port_ = 0;
  std::vector<Response> script_;
  mutable std::mutex mutex_;
  std::vector<std::string> requests_;
  std::thread thread_;
};

}  // namespace ics::lattice::testing
