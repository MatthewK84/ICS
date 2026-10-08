#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/logging/logger.hpp"
#include "ics/store/query.hpp"
#include "ics/timing/unix_socket.hpp"

namespace ics::plid {

// Serves PliQueryService (ics/v1/pli_query.proto) on a local SOCK_SEQPACKET
// socket until gRPC joins the toolchain (ICS-030), in a thread of its own: a
// client connects and sends one serialized QueryPliRequest, and reads one
// serialized QueryPliResponse per read until one has done set. Queries are
// answered one at a time from the segments in the store folder. A request
// that does not parse gets a response with done and error set. A client that
// sends nothing, or stops reading, for client_timeout is dropped.
class QueryServer {
  // Only open() makes a server.
  struct Key {
    explicit Key() = default;
  };

 public:
  // The largest request read.
  static constexpr std::size_t kMaxRequestBytes = std::size_t{1} << 16U;
  // How often the thread checks it should stop.
  static constexpr int kPollMs = 100;

  // Listens at socket, removing a file left there first, and starts the
  // thread. The logger must outlive the server. kInvalidArgument for a path
  // too long for a socket address; kUnavailable when the socket cannot be
  // made or bound, for example in a folder that does not exist.
  [[nodiscard]] static Result<std::unique_ptr<QueryServer>> open(const std::filesystem::path& socket,
                                                                 std::filesystem::path folder,
                                                                 const logging::Logger& logger,
                                                                 store::QueryLimits limits,
                                                                 Duration client_timeout = std::chrono::seconds(1));

  QueryServer(Key key, timing::Fd listener, std::filesystem::path socket, std::filesystem::path folder,
              const logging::Logger& logger, store::QueryLimits limits, Duration client_timeout);

  // Stops the thread and removes the socket.
  ~QueryServer();
  QueryServer(const QueryServer&) = delete;
  QueryServer& operator=(const QueryServer&) = delete;
  QueryServer(QueryServer&&) = delete;
  QueryServer& operator=(QueryServer&&) = delete;

  // Queries answered, whether or not they succeeded.
  [[nodiscard]] std::uint64_t served() const noexcept { return served_.load(); }

 private:
  void run();
  void serve(int client);

  timing::Fd listener_;
  std::filesystem::path socket_;
  store::Catalog catalog_;
  const logging::Logger& logger_;
  store::QueryLimits limits_;
  Duration client_timeout_;
  std::atomic<bool> stop_{false};
  std::atomic<std::uint64_t> served_{0};
  std::thread thread_;
};

}  // namespace ics::plid
