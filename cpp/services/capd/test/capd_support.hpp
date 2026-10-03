#pragma once

// Helpers for the ics-capd tests (ICS-020).

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "ics/capd/config.hpp"
#include "ics/common/units.hpp"
#include "ics/logging/json_line.hpp"
#include "ics/logging/logger.hpp"
#include "ics/timing/unix_socket.hpp"
#include "support.hpp"

namespace ics::capd::testing {

// The time every test logger stamps its lines with.
inline UtcTime fixed_clock() noexcept { return utc_from_ns(1'790'000'000'000'000'000); }

// A logger whose lines the test reads back.
struct Logged {
  std::ostringstream out;
  logging::Logger logger =
      logging::Logger::to_stream("ics-capd", logging::Level::kDebug, out, &ics::capd::testing::fixed_clock);

  [[nodiscard]] bool has(const std::string_view text) const { return out.str().find(text) != std::string::npos; }
};

// A config capturing the loopback interface with host time stamps into
// folder, rotating each second. Live capture needs CAP_NET_RAW, which the
// tests have as root in the ics-cpp container.
inline Config loopback_config(const std::filesystem::path& folder) {
  Config config;
  config.log = {logging::Level::kDebug, "ics-capd"};
  config.interfaces = {"lo"};
  config.folder = folder;
  config.snaplen = 65535;
  config.buffer_bytes = 1U << 20U;
  config.timestamps = capture::TimestampSource::kHost;
  config.rotation = {std::chrono::seconds(1), 1U << 20U};
  return config;
}

// Sends one UDP datagram of size bytes to port on the loopback interface.
inline void send_udp(const std::uint16_t port, const std::size_t size) {
  const timing::Fd socket(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const std::vector<std::byte> payload(size, std::byte{0x42});
  ::sendto(socket.get(), payload.data(), payload.size(), 0, reinterpret_cast<const sockaddr*>(&address),
           sizeof(address));
}

// ptp4l on a thread: answers each poll of its socket with answers, until
// destroyed.
class AnsweringPtp4l {
 public:
  AnsweringPtp4l(const std::filesystem::path& server, const std::filesystem::path& client,
                 const timing::testing::Answers& answers)
      : fake_(server), thread_([this, client, answers] { serve(client, answers); }) {}
  ~AnsweringPtp4l() {
    stop_ = true;
    thread_.join();
  }
  AnsweringPtp4l(const AnsweringPtp4l&) = delete;
  AnsweringPtp4l& operator=(const AnsweringPtp4l&) = delete;
  AnsweringPtp4l(AnsweringPtp4l&&) = delete;
  AnsweringPtp4l& operator=(AnsweringPtp4l&&) = delete;

 private:
  void serve(const std::filesystem::path& client, const timing::testing::Answers& answers) {
    constexpr unsigned kBitsPerByte = 8;
    while (!stop_) {
      const std::vector<std::vector<std::byte>> requests = fake_.requests();
      if (!requests.empty()) {
        // The first request's sequence number, bytes 30 and 31.
        const auto first = static_cast<std::uint16_t>((std::to_integer<unsigned>(requests[0].at(30)) << kBitsPerByte) |
                                                      std::to_integer<unsigned>(requests[0].at(31)));
        std::uint16_t sequence = first;
        for (const std::string_view hex : answers) {
          fake_.send_to(client, timing::testing::with_sequence(timing::testing::bytes(hex), sequence++));
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  timing::testing::FakePtp4l fake_;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

// ptp4l's timePropertiesDS with currentUtcOffset 37 s, marked valid
// (flags 0x3C) or not (0x38).
inline constexpr std::string_view kTimePropertiesOffset37 =
    "0d12003a0000000000000000000000000000000042d471fffe5b381400000066007f000000000000000000000000020000010006200300253c20";
inline constexpr std::string_view kTimePropertiesOffsetInvalid =
    "0d12003a0000000000000000000000000000000042d471fffe5b381400000066007f000000000000000000000000020000010006200300253820";
inline constexpr timing::testing::Answers kOffset37{timing::testing::kCurrent, timing::testing::kParent,
                                                    kTimePropertiesOffset37, timing::testing::kPort};
inline constexpr timing::testing::Answers kOffsetInvalid{timing::testing::kCurrent, timing::testing::kParent,
                                                         kTimePropertiesOffsetInvalid, timing::testing::kPort};

}  // namespace ics::capd::testing
