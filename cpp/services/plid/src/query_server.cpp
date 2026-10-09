#include "ics/plid/query_server.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "ics/common/check.hpp"
#include "ics/timing/unix_socket.hpp"
#include "ics/v1/pli_query.grpc.pb.h"

namespace ics::plid {

// PliQueryService's handler, called on gRPC's threads.
class QueryService final : public v1::PliQueryService::Service {
 public:
  QueryService(std::filesystem::path folder, const logging::Logger& logger, const store::QueryLimits limits)
      : catalog_(std::move(folder)), logger_(logger), limits_(limits) {}

  grpc::Status QueryPli(grpc::ServerContext* context, const v1::QueryPliRequest* request,
                        grpc::ServerWriter<v1::QueryPliResponse>* writer) override;

  [[nodiscard]] std::uint64_t served() const noexcept { return served_.load(); }

 private:
  // A copy of the catalog for one query, so no lock is held while it reads
  // segments or writes to its client.
  [[nodiscard]] store::Catalog borrow();
  // Keeps the summaries a query brought up to date for the next one.
  void give_back(store::Catalog catalog);

  std::mutex mutex_;
  store::Catalog catalog_;
  const logging::Logger& logger_;
  store::QueryLimits limits_;
  std::atomic<std::uint64_t> served_{0};
};

grpc::Status QueryService::QueryPli(grpc::ServerContext* /*context*/, const v1::QueryPliRequest* request,
                                    grpc::ServerWriter<v1::QueryPliResponse>* writer) {
  std::string refused;
  store::Catalog catalog = borrow();
  // A refused request gets one response, with its error, which ends the
  // stream as its status instead. A write fails once the client has gone.
  const std::uint64_t sent =
      store::answer(*request, catalog, limits_, [&refused, writer](const v1::QueryPliResponse& response) {
        refused = response.error();
        return refused.empty() && writer->Write(response);
      });
  give_back(std::move(catalog));
  if (refused.empty()) {
    logger_.debug("query_answered", {{"kind", static_cast<std::int64_t>(request->kind())},
                                     {"sent", static_cast<std::int64_t>(sent)}});
  } else {
    logger_.warn("query_refused", {{"error", refused}});
  }
  // Counted only once its line is logged, so whoever sees the count sees the
  // line.
  ++served_;
  return refused.empty() ? grpc::Status::OK : grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, refused);
}

store::Catalog QueryService::borrow() {
  const std::scoped_lock lock(mutex_);
  return catalog_;
}

void QueryService::give_back(store::Catalog catalog) {
  const std::scoped_lock lock(mutex_);
  catalog_ = std::move(catalog);
}

QueryServer::QueryServer(Key /*key*/, std::filesystem::path socket, std::unique_ptr<QueryService> service,
                         std::unique_ptr<grpc::Server> server)
    : socket_(std::move(socket)), service_(std::move(service)), server_(std::move(server)) {
  static_cast<void>(ics::check(server_ != nullptr));
}

Result<std::unique_ptr<QueryServer>> QueryServer::open(const std::filesystem::path& socket,
                                                       std::filesystem::path folder, const logging::Logger& logger,
                                                       const store::QueryLimits limits) {
  if (!timing::unix_address(socket)) {
    return fail(Error::kInvalidArgument);
  }
  std::error_code ignored;
  std::filesystem::remove(socket, ignored);
  auto service = std::make_unique<QueryService>(std::move(folder), logger, limits);
  grpc::ResourceQuota quota("ics-plid-queries");
  quota.SetMaxThreads(kMaxThreads);
  grpc::ServerBuilder builder;
  builder.AddListeningPort("unix:" + socket.native(), grpc::InsecureServerCredentials());
  builder.RegisterService(service.get());
  builder.SetMaxReceiveMessageSize(kMaxRequestBytes);
  builder.SetResourceQuota(quota);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  if (server == nullptr) {
    return fail(Error::kUnavailable);
  }
  return std::make_unique<QueryServer>(Key{}, socket, std::move(service), std::move(server));
}

QueryServer::~QueryServer() {
  server_->Shutdown(std::chrono::system_clock::now() + kShutdownGrace);
  server_->Wait();
  std::error_code ignored;
  std::filesystem::remove(socket_, ignored);
}

std::uint64_t QueryServer::served() const noexcept { return service_->served(); }

}  // namespace ics::plid
