// ics-loadgen (ICS-020): test equipment for the capture bench. It sends a
// fixed number of UDP datagrams at a fixed rate, so a capture can be checked
// against exactly what was sent.
//
// Usage: ics-loadgen DEST_IPV4 PORT COUNT RATE SIZE
//
// Sends COUNT datagrams of SIZE payload bytes to DEST_IPV4:PORT at RATE a
// second, in batches with sendmmsg, then prints "sent COUNT". Exits 0 when
// every datagram was sent, 1 on a send error and 2 for bad arguments.

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "ics/timing/unix_socket.hpp"

namespace {

constexpr std::size_t kBatch = 64;
constexpr std::uint64_t kMaxCount = 1'000'000'000;
constexpr std::uint64_t kMaxRate = 10'000'000;
constexpr std::uint64_t kMaxPort = 65'535;
constexpr std::uint64_t kMaxSize = 1'472;
constexpr std::int64_t kNanosPerSecond = 1'000'000'000;
constexpr int kExitSent = 0;
constexpr int kExitFailed = 1;
constexpr int kExitUsage = 2;

struct Plan {
  sockaddr_in destination{};
  std::uint64_t count = 0;
  std::uint64_t rate = 0;
  std::size_t size = 0;
};

// text as a whole number from 1 to max, if it is one.
std::optional<std::uint64_t> number(const std::string_view text, const std::uint64_t max) {
  std::uint64_t value = 0;
  const auto [end, error] = std::from_chars(text.begin(), text.end(), value);
  const bool whole = error == std::errc() && end == text.end();
  return whole && value >= 1 && value <= max ? std::optional(value) : std::nullopt;
}

std::optional<Plan> parse(const std::span<const char* const> args) {
  if (args.size() != 6) {
    return std::nullopt;
  }
  Plan plan;
  plan.destination.sin_family = AF_INET;
  const bool address = ::inet_pton(AF_INET, args[1], &plan.destination.sin_addr) == 1;
  const auto port = number(args[2], kMaxPort);
  const auto count = number(args[3], kMaxCount);
  const auto rate = number(args[4], kMaxRate);
  const auto size = number(args[5], kMaxSize);
  if (!address || !port || !count || !rate || !size) {
    return std::nullopt;
  }
  plan.destination.sin_port = htons(static_cast<std::uint16_t>(*port));
  plan.count = *count;
  plan.rate = *rate;
  plan.size = static_cast<std::size_t>(*size);
  return plan;
}

// Sends plan.count datagrams, a batch at a time, each batch when it is due.
int send_all(Plan plan) {
  const ics::timing::Fd socket(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  std::vector<std::byte> payload(plan.size, std::byte{0x5A});
  iovec data{payload.data(), payload.size()};
  std::array<mmsghdr, kBatch> messages{};
  for (mmsghdr& message : messages) {
    message.msg_hdr.msg_name = &plan.destination;
    message.msg_hdr.msg_namelen = sizeof(plan.destination);
    message.msg_hdr.msg_iov = &data;
    message.msg_hdr.msg_iovlen = 1;
  }
  const auto start = std::chrono::steady_clock::now();
  std::uint64_t sent = 0;
  while (sent < plan.count) {
    const auto due = std::chrono::nanoseconds(static_cast<std::int64_t>(sent) * kNanosPerSecond /
                                              static_cast<std::int64_t>(plan.rate));
    std::this_thread::sleep_until(start + due);
    const auto batch = static_cast<unsigned>(std::min<std::uint64_t>(kBatch, plan.count - sent));
    const int done = ::sendmmsg(socket.get(), messages.data(), batch, 0);
    if (done <= 0) {
      std::fprintf(stderr, "ics-loadgen: send failed after %llu datagrams\n", static_cast<unsigned long long>(sent));
      return kExitFailed;
    }
    sent += static_cast<std::uint64_t>(done);
  }
  std::printf("sent %llu\n", static_cast<unsigned long long>(sent));
  return kExitSent;
}

}  // namespace

int main(const int argc, char* argv[]) {
  const std::optional<Plan> plan = parse(std::span<const char* const>(argv, static_cast<std::size_t>(argc)));
  if (!plan) {
    std::fputs("usage: ics-loadgen DEST_IPV4 PORT COUNT RATE SIZE\n", stderr);
    return kExitUsage;
  }
  return send_all(*plan);
}
