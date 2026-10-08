#include "ics/plid/query_server.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "ics/common/check.hpp"
#include "ics/v1/pli_query.pb.h"

namespace ics::plid {
namespace {

// Sends one response as one message; false when it could not go whole.
[[nodiscard]] bool send_response(const int client, const v1::QueryPliResponse& response) {
  const std::string bytes = response.SerializeAsString();
  return ::send(client, bytes.data(), bytes.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(bytes.size());
}

void set_timeouts(const int client, const Duration timeout) noexcept {
  const auto seconds = std::chrono::floor<std::chrono::seconds>(timeout);
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(timeout - seconds);
  const timeval limit{.tv_sec = seconds.count(), .tv_usec = micros.count()};
  static_cast<void>(::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit)));
  static_cast<void>(::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit)));
}

}  // namespace

QueryServer::QueryServer(Key /*key*/, timing::Fd listener, std::filesystem::path socket, std::filesystem::path folder,
                         const logging::Logger& logger, const store::QueryLimits limits, const Duration client_timeout)
    : listener_(std::move(listener)),
      socket_(std::move(socket)),
      catalog_(std::move(folder)),
      logger_(logger),
      limits_(limits),
      client_timeout_(client_timeout),
      thread_([this] { run(); }) {}

Result<std::unique_ptr<QueryServer>> QueryServer::open(const std::filesystem::path& socket,
                                                       std::filesystem::path folder, const logging::Logger& logger,
                                                       const store::QueryLimits limits, const Duration client_timeout) {
  return timing::bound_socket(socket, timing::SocketRole::kListener).map([&](timing::Fd listener) {
    return std::make_unique<QueryServer>(Key{}, std::move(listener), socket, std::move(folder), logger, limits,
                                         client_timeout);
  });
}

QueryServer::~QueryServer() {
  stop_.store(true);
  thread_.join();
  std::error_code ignored;
  std::filesystem::remove(socket_, ignored);
}

void QueryServer::run() {
  pollfd waiting{listener_.get(), POLLIN, 0};
  while (!stop_.load()) {
    if (::poll(&waiting, 1, kPollMs) == 1) {
      const timing::Fd client(::accept4(listener_.get(), nullptr, nullptr, SOCK_CLOEXEC));
      serve(client.get());
    }
  }
}

// Reads the client's request and answers it. A client that fails to send one
// within the timeout, or sends bytes that do not parse, gets an error.
void QueryServer::serve(const int client) {
  set_timeouts(client, client_timeout_);
  std::vector<char> bytes(kMaxRequestBytes);
  const ssize_t got = ::recv(client, bytes.data(), bytes.size(), 0);
  static_cast<void>(ics::check(got <= static_cast<ssize_t>(bytes.size())));
  v1::QueryPliRequest request;
  const bool parsed = got >= 0 && request.ParseFromArray(bytes.data(), static_cast<int>(got));
  ++served_;
  if (!parsed) {
    v1::QueryPliResponse refused;
    refused.set_done(true);
    refused.set_error("the request is not a QueryPliRequest");
    static_cast<void>(send_response(client, refused));
    logger_.warn("query_refused", {{"bytes", static_cast<std::int64_t>(got)}});
    return;
  }
  const std::uint64_t sent = store::answer(request, catalog_, limits_, [client](const v1::QueryPliResponse& response) {
    return send_response(client, response);
  });
  logger_.debug("query_answered", {{"kind", static_cast<std::int64_t>(request.kind())},
                                   {"sent", static_cast<std::int64_t>(sent)}});
}

}  // namespace ics::plid
