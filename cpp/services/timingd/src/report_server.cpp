#include "ics/timingd/report_server.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "ics/common/check.hpp"
#include "ics/timing/unix_socket.hpp"
#include "ics/timingd/report_board.hpp"
#include "ics/v1/time_quality_service.grpc.pb.h"

namespace ics::timingd {
namespace {

// gRPC runs each subscriber's stream on a thread of its own; the spare
// threads let it refuse a subscriber past the limit instead of queueing it.
constexpr int kMaxThreads = static_cast<int>(ReportServer::kMaxSubscribers) + 4;
// The largest request taken: WatchTimeQualityRequest has no fields.
constexpr int kMaxRequestBytes = 1024;

}  // namespace

// TimeQualityService's handler, called on gRPC's threads.
class ReportService final : public v1::TimeQualityService::Service {
 public:
  grpc::Status WatchTimeQuality(grpc::ServerContext* context, const v1::WatchTimeQualityRequest* request,
                                grpc::ServerWriter<v1::WatchTimeQualityResponse>* writer) override;

  [[nodiscard]] ReportBoard& board() noexcept { return board_; }
  [[nodiscard]] std::size_t subscribers() const noexcept { return subscribers_.load(); }

 private:
  // Counts a new subscriber in; false, counting nothing, when there are
  // already kMaxSubscribers.
  [[nodiscard]] bool admit() noexcept;
  // Writes each newer report until the client goes, a write fails or the
  // board closes.
  void stream(const grpc::ServerContext& context, grpc::ServerWriter<v1::WatchTimeQualityResponse>& writer);

  ReportBoard board_;
  std::atomic<std::size_t> subscribers_{0};
};

grpc::Status ReportService::WatchTimeQuality(grpc::ServerContext* context,
                                             const v1::WatchTimeQualityRequest* /*request*/,
                                             grpc::ServerWriter<v1::WatchTimeQualityResponse>* writer) {
  if (!admit()) {
    return {grpc::StatusCode::RESOURCE_EXHAUSTED, "ics-timingd is serving its maximum number of subscribers"};
  }
  stream(*context, *writer);
  // admit counted this subscriber in.
  static_cast<void>(ics::check(subscribers_.fetch_sub(1) > 0));
  return grpc::Status::OK;
}

bool ReportService::admit() noexcept {
  if (subscribers_.fetch_add(1) < ReportServer::kMaxSubscribers) {
    return true;
  }
  --subscribers_;
  return false;
}

void ReportService::stream(const grpc::ServerContext& context,
                           grpc::ServerWriter<v1::WatchTimeQualityResponse>& writer) {
  std::vector<std::byte> bytes;
  v1::WatchTimeQualityResponse response;
  std::uint64_t seen = 0;
  for (bool open = true; open;) {
    const ReportBoard::Taken taken = board_.take_newer(seen, bytes, ReportServer::kWakeInterval);
    if (taken.wait == ReportBoard::Wait::kReport) {
      // The board numbers its reports in order, and gives only newer ones.
      static_cast<void>(ics::check(taken.number > seen));
      seen = taken.number;
      // The board holds what the poll loop serialized, so it always parses.
      static_cast<void>(
          ics::check(response.mutable_report()->ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))));
      open = writer.Write(response);
    } else {
      open = taken.wait == ReportBoard::Wait::kTimeout && !context.IsCancelled();
    }
  }
}

ReportServer::ReportServer(Key /*key*/, std::filesystem::path socket, std::unique_ptr<ReportService> service,
                           std::unique_ptr<grpc::Server> server)
    : socket_(std::move(socket)), service_(std::move(service)), server_(std::move(server)) {
  static_cast<void>(ics::check(server_ != nullptr));
}

Result<std::unique_ptr<ReportServer>> ReportServer::open(const std::filesystem::path& socket) {
  if (!timing::unix_address(socket)) {
    return fail(Error::kInvalidArgument);
  }
  std::error_code ignored;
  std::filesystem::remove(socket, ignored);
  auto service = std::make_unique<ReportService>();
  grpc::ResourceQuota quota("ics-timingd-subscribers");
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
  return std::make_unique<ReportServer>(Key{}, socket, std::move(service), std::move(server));
}

ReportServer::~ReportServer() {
  service_->board().close();
  server_->Shutdown(std::chrono::system_clock::now() + kShutdownGrace);
  server_->Wait();
  std::error_code ignored;
  std::filesystem::remove(socket_, ignored);
}

void ReportServer::reserve(const std::size_t bytes) { service_->board().reserve(bytes); }

void ReportServer::post(const std::span<const std::byte> report) noexcept { service_->board().post(report); }

std::size_t ReportServer::subscribers() const noexcept { return service_->subscribers(); }

}  // namespace ics::timingd
