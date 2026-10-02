#include "ics/timing/ptp_client.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <variant>

#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "ics/timing/ptp_management.hpp"

namespace ics::timing {
namespace {

// The requests, in the order of DataSet's alternatives: the answer to
// request i holds alternative i.
constexpr std::array<DataSetId, 4> kRequests{DataSetId::kCurrent, DataSetId::kParent, DataSetId::kTimeProperties,
                                             DataSetId::kPort};
// Datagrams read per poll at most: the four answers, with room for late
// answers to an earlier poll and for anything else on the socket.
constexpr std::size_t kMaxDatagrams = 32;
// Larger than any management message ptp4l sends for these data sets.
constexpr std::size_t kBufferSize = 512;

// The answers to one poll, as they arrive.
struct Answers {
  std::optional<CurrentDataSet> current;
  std::optional<ParentDataSet> parent;
  std::optional<TimePropertiesDataSet> time;
  std::optional<PortDataSet> port;

  // Keeps response if it answers request (first + its alternative's index).
  void take(const Response& response, const std::uint16_t first) noexcept {
    if (static_cast<std::uint16_t>(response.sequence - first) != response.data.index()) {
      return;
    }
    std::visit([this](const auto& data) { keep(data); }, response.data);
  }

  void keep(const CurrentDataSet& data) noexcept { current = data; }
  void keep(const ParentDataSet& data) noexcept { parent = data; }
  void keep(const TimePropertiesDataSet& data) noexcept { time = data; }
  // A clock with several ports answers once for each; keep the first.
  void keep(const PortDataSet& data) noexcept { port = port.value_or(data); }

  [[nodiscard]] bool complete() const noexcept {
    return current.has_value() && parent.has_value() && time.has_value() && port.has_value();
  }
};

// Waits until deadline for a datagram and reads it into buffer: its size, or
// kUnavailable when none comes in time.
[[nodiscard]] Result<std::size_t> receive(const int socket, const std::span<std::byte> buffer,
                                          const SteadyTime deadline) noexcept {
  const auto wait = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
  pollfd ready{socket, POLLIN, 0};
  const int events = ::poll(&ready, 1, static_cast<int>(std::max<std::int64_t>(wait.count(), 0)));
  const ssize_t size = events > 0 ? ::recv(socket, buffer.data(), buffer.size(), MSG_DONTWAIT) : -1;
  if (size < 0) {
    return fail(Error::kUnavailable);
  }
  return static_cast<std::size_t>(size);
}

}  // namespace

PtpClient::PtpClient(Fd socket, const sockaddr_un server, std::filesystem::path client,
                     const std::uint8_t domain) noexcept
    : socket_(std::move(socket)), server_(server), client_(std::move(client)), domain_(domain) {}

PtpClient::~PtpClient() {
  if (socket_.valid()) {
    std::error_code ignored;
    std::filesystem::remove(client_, ignored);
  }
}

Result<PtpClient> PtpClient::open(const std::filesystem::path& server, const std::filesystem::path& client,
                                  const std::uint8_t domain) noexcept {
  const Result<sockaddr_un> server_address = unix_address(server);
  if (!server_address) {
    return fail(server_address.error());
  }
  Result<Fd> socket = bound_socket(client, SocketRole::kDatagram);
  if (!socket) {
    return fail(socket.error());
  }
  return PtpClient(std::move(*socket), *server_address, client, domain);
}

Status PtpClient::send_requests(const std::uint16_t first) const noexcept {
  for (std::size_t i = 0; i < kRequests.size(); ++i) {
    const GetRequest request = encode_get(kRequests[i], static_cast<std::uint16_t>(first + i), domain_);
    const ssize_t sent = ::sendto(socket_.get(), request.data(), request.size(), MSG_DONTWAIT,
                                  reinterpret_cast<const sockaddr*>(&server_), sizeof(server_));
    if (sent != static_cast<ssize_t>(request.size())) {
      return fail(Error::kUnavailable);
    }
  }
  return {};
}

Result<Snapshot> PtpClient::poll(const Duration timeout) noexcept {
  const std::uint16_t first = sequence_;
  sequence_ = static_cast<std::uint16_t>(sequence_ + kRequests.size());
  const Status sent = send_requests(first);
  if (!sent) {
    return fail(sent.error());
  }
  const SteadyTime deadline = std::chrono::steady_clock::now() + timeout;
  std::array<std::byte, kBufferSize> buffer{};
  Answers answers;
  for (std::size_t count = 0; count < kMaxDatagrams && !answers.complete(); ++count) {
    const Result<std::size_t> size = receive(socket_.get(), buffer, deadline);
    if (!size) {
      return fail(size.error());
    }
    const Result<Response> response = decode_response(std::span<const std::byte>(buffer).first(*size));
    if (response) {
      answers.take(*response, first);
    }
  }
  if (!answers.complete()) {
    return fail(Error::kUnavailable);
  }
  return Snapshot{*answers.port, *answers.current, *answers.parent, *answers.time};
}

}  // namespace ics::timing
