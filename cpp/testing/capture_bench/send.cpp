// ics-capture-send (ICS-020, deploy/capture-bench): sends COUNT UDP datagrams
// to ADDRESS:PORT at RATE a second, numbered 0 to COUNT - 1, in batches of up
// to kBatch with sendmmsg. A sender that falls more than kMaxLag behind starts
// its schedule again from now, so it never bursts to catch up. Prints the
// number sent, and exits 1 if the system refused any.
//
// Usage: ics-capture-send ADDRESS PORT COUNT RATE

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "sequence.hpp"

namespace {

using ics::capture_bench::kPayloadBytes;
using ics::capture_bench::kSequenceBytes;

constexpr std::size_t kBatch = 64;
constexpr std::chrono::nanoseconds kMaxLag = std::chrono::milliseconds(10);
constexpr std::uint64_t kMaxPort = 65535;
constexpr std::int64_t kNanosPerSecond = 1'000'000'000;

struct Plan {
  sockaddr_in to{};
  std::uint64_t count = 0;
  std::uint64_t rate = 0;
};

// The plan on the command line, or nothing if it is not one.
std::optional<Plan> parse(const std::span<const char* const> args) {
  if (args.size() != 5) {
    return std::nullopt;
  }
  Plan plan;
  plan.to.sin_family = AF_INET;
  const bool address = ::inet_pton(AF_INET, args[1], &plan.to.sin_addr) == 1;
  const std::optional<std::uint64_t> port = ics::capture_bench::number(args[2]);
  const std::optional<std::uint64_t> count = ics::capture_bench::number(args[3]);
  const std::optional<std::uint64_t> rate = ics::capture_bench::number(args[4]);
  if (!address || !port || *port > kMaxPort || !count || !rate || *rate == 0) {
    return std::nullopt;
  }
  plan.to.sin_port = htons(static_cast<std::uint16_t>(*port));
  plan.count = *count;
  plan.rate = *rate;
  return plan;
}

// One batch of datagrams, sent with one sendmmsg call. The messages point
// into the batch's own members, so it is neither copied nor moved.
class Batch {
 public:
  explicit Batch(const sockaddr_in& to) : to_(to) {
    for (std::size_t i = 0; i < kBatch; ++i) {
      vectors_[i] = iovec{payloads_[i].data(), kPayloadBytes};
      messages_[i].msg_hdr.msg_name = &to_;
      messages_[i].msg_hdr.msg_namelen = sizeof(to_);
      messages_[i].msg_hdr.msg_iov = &vectors_[i];
      messages_[i].msg_hdr.msg_iovlen = 1;
    }
  }

  Batch(const Batch&) = delete;
  Batch& operator=(const Batch&) = delete;
  Batch(Batch&&) = delete;
  Batch& operator=(Batch&&) = delete;
  ~Batch() = default;

  // Sends size datagrams numbered from first: true when all went.
  [[nodiscard]] bool send(const int socket, const std::uint64_t first, const std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
      ics::capture_bench::put_sequence(std::span(payloads_[i]).first<kSequenceBytes>(), first + i);
    }
    std::size_t sent = 0;
    for (int done = 0; sent < size; sent += static_cast<std::size_t>(done)) {
      done = ::sendmmsg(socket, std::span(messages_).subspan(sent).data(), static_cast<unsigned>(size - sent), 0);
      if (done <= 0) {
        return false;
      }
    }
    return true;
  }

 private:
  sockaddr_in to_;
  std::array<std::array<std::byte, kPayloadBytes>, kBatch> payloads_{};
  std::array<iovec, kBatch> vectors_{};
  std::array<mmsghdr, kBatch> messages_{};
};

timespec to_timespec(const std::chrono::steady_clock::time_point time) {
  const std::int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
  return timespec{static_cast<time_t>(ns / kNanosPerSecond), static_cast<long>(ns % kNanosPerSecond)};
}

// Sends the plan's datagrams on schedule: how many went.
std::uint64_t send_all(const int socket, Plan plan) {
  Batch batch(plan.to);
  auto next = std::chrono::steady_clock::now();
  std::uint64_t sent = 0;
  while (sent < plan.count) {
    const std::size_t size = std::min<std::uint64_t>(kBatch, plan.count - sent);
    if (!batch.send(socket, sent, size)) {
      return sent;
    }
    sent += size;
    next += std::chrono::nanoseconds(static_cast<std::int64_t>(size) * kNanosPerSecond /
                                     static_cast<std::int64_t>(plan.rate));
    next = std::max(next, std::chrono::steady_clock::now() - kMaxLag);
    const timespec until = to_timespec(next);
    static_cast<void>(::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &until, nullptr));
  }
  return sent;
}

}  // namespace

int main(const int argc, char* argv[]) {
  const std::optional<Plan> plan = parse(std::span<const char* const>(argv, static_cast<std::size_t>(argc)));
  if (!plan) {
    std::fputs("usage: ics-capture-send ADDRESS PORT COUNT RATE\n", stderr);
    return 2;
  }
  const int socket = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  const std::uint64_t sent = socket < 0 ? 0 : send_all(socket, *plan);
  std::printf("%llu\n", static_cast<unsigned long long>(sent));
  ::close(socket);
  return sent == plan->count ? 0 : 1;
}
