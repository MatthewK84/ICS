#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>

#include "ics/common/error.hpp"
#include "ics/logging/logger.hpp"
#include "ics/store/query.hpp"

namespace grpc {
class Server;
}  // namespace grpc

namespace ics::plid {

class QueryService;

// Serves PliQueryService (ics/v1/pli_query.proto) over gRPC on a Unix-domain
// socket (#146), from gRPC's own threads. A query's batches go out as the
// stream's messages, earliest first, the last with done set; a request the
// store refuses ends the stream with INVALID_ARGUMENT and the reason. Queries
// run side by side, each from its own copy of the store's segment catalog,
// so a client that stops reading holds up only its own query, until it goes
// away or the server stops.
class QueryServer {
  // Only open() makes a server.
  struct Key {
    explicit Key() = default;
  };

 public:
  // The largest request taken.
  static constexpr int kMaxRequestBytes = 1 << 16;
  // The most threads gRPC runs queries on; a query beyond them waits.
  static constexpr int kMaxThreads = 8;
  // How long stopping waits for the queries running before it cancels them.
  static constexpr std::chrono::milliseconds kShutdownGrace{1000};

  // Listens at socket, removing a file left there first, and answers queries
  // from the segments in folder. The logger must outlive the server.
  // kInvalidArgument for a path too long for a socket address; kUnavailable
  // when the socket cannot be made, for example in a folder that does not
  // exist.
  [[nodiscard]] static Result<std::unique_ptr<QueryServer>> open(const std::filesystem::path& socket,
                                                                 std::filesystem::path folder,
                                                                 const logging::Logger& logger,
                                                                 store::QueryLimits limits);

  QueryServer(Key key, std::filesystem::path socket, std::unique_ptr<QueryService> service,
              std::unique_ptr<grpc::Server> server);

  // Stops the server, cancelling the queries still running after
  // kShutdownGrace, and removes the socket.
  ~QueryServer();
  QueryServer(const QueryServer&) = delete;
  QueryServer& operator=(const QueryServer&) = delete;
  QueryServer(QueryServer&&) = delete;
  QueryServer& operator=(QueryServer&&) = delete;

  // Queries answered, whether or not they succeeded.
  [[nodiscard]] std::uint64_t served() const noexcept;

 private:
  std::filesystem::path socket_;
  // Declared before server_, so it outlives the server's threads.
  std::unique_ptr<QueryService> service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace ics::plid
