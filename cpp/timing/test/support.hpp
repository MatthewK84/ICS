#pragma once

// Helpers for the ics::timing tests (ICS-019).

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "ics/timing/unix_socket.hpp"

namespace ics::timing::testing {

// Responses ptp4l 4.0 sent on its read-only management socket, from a station
// following a software grandmaster (deploy/timing-bench), with the
// grandmaster locked to GNSS and, for the _holdover ones, after it lost GNSS.
// Their sequence numbers are 100 to 103; error is the answer to a SET.
inline constexpr std::string_view kCurrent =
    "0d1200480000000000000000000000000000000042d471fffe5b381400000064007f0000000000000000000000000200000100142001"
    "0001ffffffffff4100000000000005af0000";
inline constexpr std::string_view kParent =
    "0d1200560000000000000000000000000000000042d471fffe5b381400000065007f0000000000000000000000000200000100222002"
    "ce07d8fffe0f38b100010000ffff7fffffff8006214e5d80ce07d8fffe0f38b1";
inline constexpr std::string_view kParentHoldover =
    "0d1200560000000000000000000000000000000042d471fffe5b381400000065007f0000000000000000000000000200000100222002"
    "ce07d8fffe0f38b100010000ffff7fffffff8007214e5d80ce07d8fffe0f38b1";
inline constexpr std::string_view kTimeProperties =
    "0d12003a0000000000000000000000000000000042d471fffe5b381400000066007f000000000000000000000000020000010006200300003c20";
inline constexpr std::string_view kTimePropertiesHoldover =
    "0d12003a0000000000000000000000000000000042d471fffe5b381400000066007f000000000000000000000000020000010006200300002ca0";
inline constexpr std::string_view kPort =
    "0d1200500000000000000000000000000000000042d471fffe5b381400010067007f00000000000000000000000002000001001c2004"
    "42d471fffe5b3814000109000000000000000000010300010002";
inline constexpr std::string_view kError =
    "0d12003c0000000000000000000000000000000042d471fffe5b3814000000c8007f0000000000000000000000000200000200080003"
    "200300000000";
// The answer to a GET for the default data set, which ics-timingd never asks for.
inline constexpr std::string_view kDefault =
    "0d12004a0000000000000000000000000000000042d471fffe5b381400000068007f0000000000000000000000000200000100162000"
    "0300000180fffeffff8042d471fffe5b38140000";

// The answers to one poll, in the order ics::timing::PtpClient asks.
using Answers = std::array<std::string_view, 4>;
inline constexpr Answers kLocked{kCurrent, kParent, kTimeProperties, kPort};
inline constexpr Answers kHoldover{kCurrent, kParentHoldover, kTimePropertiesHoldover, kPort};

inline std::vector<std::byte> bytes(const std::string_view hex) {
  std::vector<std::byte> out;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    out.push_back(static_cast<std::byte>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
  }
  return out;
}

// message with its sequence number (bytes 30 and 31) replaced.
inline std::vector<std::byte> with_sequence(std::vector<std::byte> message, const std::uint16_t sequence) {
  constexpr unsigned kBitsPerByte = 8;
  message.at(30) = static_cast<std::byte>(sequence >> kBitsPerByte);
  message.at(31) = static_cast<std::byte>(sequence & 0xFFU);
  return message;
}

// A folder for one test's sockets, removed with everything in it at the end.
// Its path is short, so socket paths in it fit a socket address.
class TempDir {
 public:
  TempDir() : path_(std::filesystem::temp_directory_path() / ("ics-timing-" + std::to_string(::getpid()))) {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;

  [[nodiscard]] std::filesystem::path operator/(const std::string_view name) const { return path_ / name; }

 private:
  std::filesystem::path path_;
};

// ptp4l's end of the socket: it records the requests and sends prepared
// answers, which wait in the client's socket until it polls.
class FakePtp4l {
 public:
  explicit FakePtp4l(const std::filesystem::path& path)
      : socket_(bound_socket(path, SocketRole::kDatagram).value()) {}

  void send_to(const std::filesystem::path& client, const std::vector<std::byte>& message) const {
    const sockaddr_un address = unix_address(client).value();
    ::sendto(socket_.get(), message.data(), message.size(), 0, reinterpret_cast<const sockaddr*>(&address),
             sizeof(address));
  }

  // Reads the requests waiting, as ptp4l would, then sends the answers to
  // the poll whose first request was sequence first. Unread requests would
  // fill the socket's queue (net.unix.max_dgram_qlen, 10 by default).
  void answer(const std::filesystem::path& client, const std::uint16_t first, const Answers& answers = kLocked) const {
    requests();
    std::uint16_t sequence = first;
    for (const std::string_view hex : answers) {
      send_to(client, with_sequence(bytes(hex), sequence++));
    }
  }

  std::vector<std::vector<std::byte>> requests() const {
    std::vector<std::vector<std::byte>> received;
    std::array<std::byte, 512> buffer{};
    for (ssize_t size = 0; (size = ::recv(socket_.get(), buffer.data(), buffer.size(), MSG_DONTWAIT)) > 0;) {
      received.emplace_back(buffer.begin(), buffer.begin() + size);
    }
    return received;
  }

 private:
  Fd socket_;
};

// Lowers the process's descriptor limit while it lives, so the next socket
// cannot be made: the one way to make socket(2) fail on demand.
class DescriptorLimit {
 public:
  DescriptorLimit() {
    ::getrlimit(RLIMIT_NOFILE, &saved_);
    const int lowest_free = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    ::close(lowest_free);
    rlimit lowered = saved_;
    lowered.rlim_cur = static_cast<rlim_t>(lowest_free);
    ::setrlimit(RLIMIT_NOFILE, &lowered);
  }
  ~DescriptorLimit() { ::setrlimit(RLIMIT_NOFILE, &saved_); }
  DescriptorLimit(const DescriptorLimit&) = delete;
  DescriptorLimit& operator=(const DescriptorLimit&) = delete;
  DescriptorLimit(DescriptorLimit&&) = delete;
  DescriptorLimit& operator=(DescriptorLimit&&) = delete;

 private:
  rlimit saved_{};
};

}  // namespace ics::timing::testing
