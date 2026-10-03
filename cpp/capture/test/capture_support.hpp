#pragma once

// Helpers for the ics::capture tests (ICS-020).

#include <algorithm>
#include <array>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <netinet/in.h>
#include <pcap/pcap.h>
#include <sys/resource.h>
#include <sys/socket.h>

#include "ics/capture/pcap_file.hpp"
#include "ics/common/units.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::capture::testing {

// Marks the test's datagrams among anything else on the loopback interface.
inline constexpr std::string_view kMarker = "ics-capture-test-datagram";


// A UDP socket bound to a free loopback port, and a sender to it.
class Loopback {
 public:
  Loopback() {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    EXPECT_EQ(::bind(receiver_.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
    EXPECT_EQ(::getsockname(receiver_.get(), reinterpret_cast<sockaddr*>(&address), &size), 0);
    address_ = address;
  }

  void send(const int count) const {
    for (int i = 0; i < count; ++i) {
      ::sendto(sender_.get(), kMarker.data(), kMarker.size(), 0, reinterpret_cast<const sockaddr*>(&address_),
               sizeof(address_));
    }
  }

 private:
  ics::timing::Fd receiver_{::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)};
  ics::timing::Fd sender_{::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)};
  sockaddr_in address_{};
};


// The bytes of the file at path.
inline std::vector<std::byte> file_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out;
  std::ranges::transform(chars, std::back_inserter(out), [](const char c) { return static_cast<std::byte>(c); });
  return out;
}

// The text of the file at path.
inline std::string file_text(const std::filesystem::path& path) {
  std::ifstream in(path);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// A packet as libpcap read it back from a file.
struct ReadPacket {
  std::int64_t utc_ns = 0;
  std::uint32_t wire_length = 0;
  std::vector<std::byte> data;
};

// The packets in the pcap file at path, read by libpcap with nanosecond
// time stamps; empty if libpcap cannot read it.
inline std::vector<ReadPacket> read_packets(const std::filesystem::path& path) {
  constexpr std::int64_t kNanosPerSecond = 1'000'000'000;
  std::array<char, PCAP_ERRBUF_SIZE> error{};
  pcap_t* const file = pcap_open_offline_with_tstamp_precision(path.c_str(), PCAP_TSTAMP_PRECISION_NANO, error.data());
  std::vector<ReadPacket> out;
  pcap_pkthdr* header = nullptr;
  const u_char* bytes = nullptr;
  while (file != nullptr && pcap_next_ex(file, &header, &bytes) == 1) {
    const auto* const begin = reinterpret_cast<const std::byte*>(bytes);
    out.push_back({header->ts.tv_sec * kNanosPerSecond + header->ts.tv_usec, header->len,
                   std::vector<std::byte>(begin, std::next(begin, header->caplen))});
  }
  if (file != nullptr) {
    pcap_close(file);
  }
  return out;
}

// How many of the test's datagrams the files hold.
inline std::size_t marked(const std::vector<std::filesystem::path>& files) {
  std::size_t count = 0;
  for (const std::filesystem::path& file : files) {
    for (const auto& packet : ics::capture::testing::read_packets(file)) {
      const std::string text(reinterpret_cast<const char*>(packet.data.data()), packet.data.size());
      count += text.ends_with(kMarker) ? 1 : 0;
    }
  }
  return count;
}

// Limits the size of files this process writes while it lives, and ignores
// the SIGXFSZ that writing past the limit raises, so the write fails instead.
class FileSizeLimit {
 public:
  explicit FileSizeLimit(const rlim_t bytes) {
    ::getrlimit(RLIMIT_FSIZE, &saved_);
    previous_ = std::signal(SIGXFSZ, SIG_IGN);
    rlimit lowered = saved_;
    lowered.rlim_cur = bytes;
    ::setrlimit(RLIMIT_FSIZE, &lowered);
  }
  ~FileSizeLimit() {
    ::setrlimit(RLIMIT_FSIZE, &saved_);
    std::signal(SIGXFSZ, previous_);
  }
  FileSizeLimit(const FileSizeLimit&) = delete;
  FileSizeLimit& operator=(const FileSizeLimit&) = delete;
  FileSizeLimit(FileSizeLimit&&) = delete;
  FileSizeLimit& operator=(FileSizeLimit&&) = delete;

 private:
  rlimit saved_{};
  void (*previous_)(int) = SIG_DFL;
};

}  // namespace ics::capture::testing
