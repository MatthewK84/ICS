// ics-capture-verify (ICS-020, deploy/capture-bench): checks that a pcap file
// ics-capd wrote holds the bench's datagrams in order, numbered from FIRST
// with none missing or repeated. Prints the number the next file must start
// with; exits 1 naming the first packet out of order, or a file it cannot
// read.
//
// Usage: ics-capture-verify FILE FIRST

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>

#include "ics/capture/capture.hpp"
#include "ics/common/error.hpp"
#include "sequence.hpp"

namespace {

using ics::capture_bench::kSequenceBytes;
using ics::capture_bench::kSequenceOffset;

constexpr int kBatch = 4096;

// Expects each packet to carry the next number.
class Checker final : public ics::capture::PacketSink {
 public:
  explicit Checker(const std::uint64_t first) : next_(first) {}

  ics::Status accept(const ics::capture::Packet& packet) override {
    if (packet.bytes.size() < kSequenceOffset + kSequenceBytes) {
      std::fprintf(stderr, "packet %" PRIu64 " is too short to hold a sequence number\n", next_);
      return ics::fail(ics::Error::kMalformed);
    }
    const std::uint64_t found =
        ics::capture_bench::get_sequence(packet.bytes.subspan<kSequenceOffset, kSequenceBytes>());
    if (found != next_) {
      std::fprintf(stderr, "expected packet %" PRIu64 ", found %" PRIu64 "\n", next_, found);
      return ics::fail(ics::Error::kMalformed);
    }
    ++next_;
    return {};
  }

  [[nodiscard]] std::uint64_t next() const noexcept { return next_; }

 private:
  std::uint64_t next_;
};

// Checks every packet in capture: true when all were in order.
bool check_all(ics::capture::Capture& capture, Checker& checker) {
  for (ics::Result<std::size_t> count = std::size_t{1}; count; count = capture.dispatch(checker, kBatch)) {
    if (*count == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main(const int argc, char* argv[]) {
  const std::span<const char* const> args(argv, static_cast<std::size_t>(argc));
  const std::optional<std::uint64_t> first = args.size() == 3 ? ics::capture_bench::number(args[2]) : std::nullopt;
  if (!first) {
    std::fputs("usage: ics-capture-verify FILE FIRST\n", stderr);
    return 2;
  }
  std::string reason;
  ics::Result<ics::capture::Capture> capture = ics::capture::Capture::open_file(args[1], reason);
  if (!capture) {
    std::fprintf(stderr, "%s\n", reason.c_str());
    return 1;
  }
  Checker checker(*first);
  if (!check_all(*capture, checker)) {
    std::fprintf(stderr, "%s: not every packet is in order\n", args[1]);
    return 1;
  }
  std::printf("%" PRIu64 "\n", checker.next());
  return 0;
}
