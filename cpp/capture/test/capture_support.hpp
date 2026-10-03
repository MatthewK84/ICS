#pragma once

// Helpers for the ics::capture tests (ICS-020).

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sys/resource.h>

#include "ics/capture/capture.hpp"
#include "ics/capture/sha256.hpp"
#include "ics/common/error.hpp"

namespace ics::capture::testing {

// Every byte of the file at path.
inline std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(chars.size());
  std::ranges::transform(chars, out.begin(), [](const char c) { return static_cast<std::byte>(c); });
  return out;
}

// The text of the file at path.
inline std::string read_text(const std::filesystem::path& path) {
  std::ifstream in(path);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The SHA-256 of bytes, in hexadecimal.
inline std::string sha256_hex(const std::vector<std::byte>& bytes) {
  Sha256 hash = Sha256::make().value();
  EXPECT_TRUE(hash.update(bytes).has_value());
  return to_hex(hash.finish().value());
}

// A packet sink that keeps a copy of each packet, and answers each with
// answer.
class Collector : public PacketSink {
 public:
  struct Kept {
    UtcTime time;
    std::uint32_t original_length = 0;
    std::vector<std::byte> bytes;
  };

  explicit Collector(const Status answer = {}) : answer_(answer) {}

  Status accept(const Packet& packet) override {
    kept.push_back(Kept{packet.time, packet.original_length, {packet.bytes.begin(), packet.bytes.end()}});
    return answer_;
  }

  std::vector<Kept> kept;

 private:
  Status answer_;
};

// Lowers the largest file this process may write while it lives, so a write
// stops part way and the next one fails: the way to make write(2) fail on
// demand. SIGXFSZ is ignored meanwhile, so the write fails with EFBIG.
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
  void (*previous_)(int) = nullptr;
};

}  // namespace ics::capture::testing
