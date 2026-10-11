#include "ics/camd/command_server.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "ics/camd/offloader.hpp"
#include "ics/camd/quality_watch.hpp"
#include "ics/common/check.hpp"
#include "ics/timing/unix_socket.hpp"
#include "ics/v1/camera_service.grpc.pb.h"

namespace ics::camd {
namespace {

// One command at a time, and room to refuse another while it runs.
constexpr int kMaxThreads = 4;
// The largest request taken: an ArmAndOffloadRequest is a run ID of 64
// characters at most.
constexpr int kMaxRequestBytes = 1024;

[[nodiscard]] std::uint64_t frames(const camera::OffloadReport& report) noexcept {
  std::uint64_t out = 0;
  for (const camera::SegmentCheck& segment : report.segments) {
    out += segment.frames;
  }
  return out;
}

// One camera's part of the response.
void add_camera(const CameraOutcome& outcome, v1::ArmAndOffloadResponse& response) {
  v1::CameraOffload& camera = *response.add_cameras();
  camera.set_camera_id(outcome.camera_id);
  camera.set_verified(!outcome.error.has_value() && outcome.report.verified);
  camera.set_segments(static_cast<std::uint32_t>(outcome.report.segments.size()));
  camera.set_frames(frames(outcome.report));
  for (const std::filesystem::path& file : outcome.files) {
    camera.add_files(file.native());
  }
  if (outcome.error) {
    camera.set_error(std::string(to_string(*outcome.error)));
  }
}

}  // namespace

// CameraService's handler, called on gRPC's threads.
class CommandService final : public v1::CameraService::Service {
 public:
  CommandService(const Config& config, const logging::Logger& logger) noexcept : config_(config), logger_(logger) {}

  grpc::Status ArmAndOffload(grpc::ServerContext* context, const v1::ArmAndOffloadRequest* request,
                             v1::ArmAndOffloadResponse* response) override;

 private:
  // Makes the run's folder and offloads every camera into it.
  [[nodiscard]] grpc::Status offload(const std::string& run_id, v1::ArmAndOffloadResponse& response);
  // Logs how each camera's offload went.
  void log_outcomes(const std::string& run_id, const std::vector<CameraOutcome>& outcomes) const;

  const Config& config_;
  const logging::Logger& logger_;
  std::atomic<bool> busy_{false};
};

grpc::Status CommandService::ArmAndOffload(grpc::ServerContext* /*context*/,
                                           const v1::ArmAndOffloadRequest* request,
                                           v1::ArmAndOffloadResponse* response) {
  if (!folder_name(request->run_id())) {
    return {grpc::StatusCode::INVALID_ARGUMENT,
            "run_id must be 1 to 64 letters, digits, '.', '_' or '-', and not \".\" or \"..\""};
  }
  if (busy_.exchange(true)) {
    return {grpc::StatusCode::FAILED_PRECONDITION, "another ArmAndOffload is running"};
  }
  const grpc::Status status = offload(request->run_id(), *response);
  busy_.store(false);
  return status;
}

grpc::Status CommandService::offload(const std::string& run_id, v1::ArmAndOffloadResponse& response) {
  const std::filesystem::path folder = config_.offload_folder / run_id;
  std::error_code error;
  if (std::filesystem::exists(folder, error)) {
    return {grpc::StatusCode::ALREADY_EXISTS, "the run's folder " + folder.native() + " exists"};
  }
  std::filesystem::create_directories(folder, error);
  if (error) {
    return {grpc::StatusCode::UNAVAILABLE, "cannot make the run's folder " + folder.native()};
  }
  QualityWatch quality(config_.time_socket);
  if (!quality.wait_for_report(config_.time_wait)) {
    logger_.warn("no_time_quality", {{"run_id", run_id}, {"time_socket", config_.time_socket.native()}});
  }
  logger_.info("offload_started",
               {{"run_id", run_id}, {"cameras", static_cast<std::int64_t>(config_.cameras.size())}});
  const std::vector<CameraOutcome> outcomes = offload_all(config_, folder, quality, logging::system_now());
  response.set_folder(folder.native());
  for (const CameraOutcome& outcome : outcomes) {
    add_camera(outcome, response);
  }
  log_outcomes(run_id, outcomes);
  return grpc::Status::OK;
}

void CommandService::log_outcomes(const std::string& run_id, const std::vector<CameraOutcome>& outcomes) const {
  for (const CameraOutcome& outcome : outcomes) {
    if (outcome.error) {
      logger_.error("offload_failed",
                    {{"run_id", run_id}, {"camera", outcome.camera_id}, {"error", to_string(*outcome.error)}});
    } else {
      logger_.info("offloaded", {{"run_id", run_id},
                                 {"camera", outcome.camera_id},
                                 {"segments", static_cast<std::int64_t>(outcome.report.segments.size())},
                                 {"verified", outcome.report.verified}});
    }
  }
}

CommandServer::CommandServer(Key /*key*/, std::filesystem::path socket, std::unique_ptr<CommandService> service,
                             std::unique_ptr<grpc::Server> server)
    : socket_(std::move(socket)), service_(std::move(service)), server_(std::move(server)) {
  static_cast<void>(ics::check(server_ != nullptr));
}

Result<std::unique_ptr<CommandServer>> CommandServer::open(const Config& config, const logging::Logger& logger) {
  if (!timing::unix_address(config.command_socket)) {
    return fail(Error::kInvalidArgument);
  }
  std::error_code ignored;
  std::filesystem::remove(config.command_socket, ignored);
  auto service = std::make_unique<CommandService>(config, logger);
  grpc::ResourceQuota quota("ics-camd-commands");
  quota.SetMaxThreads(kMaxThreads);
  grpc::ServerBuilder builder;
  builder.AddListeningPort("unix:" + config.command_socket.native(), grpc::InsecureServerCredentials());
  builder.RegisterService(service.get());
  builder.SetMaxReceiveMessageSize(kMaxRequestBytes);
  builder.SetResourceQuota(quota);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  if (server == nullptr) {
    return fail(Error::kUnavailable);
  }
  return std::make_unique<CommandServer>(Key{}, config.command_socket, std::move(service), std::move(server));
}

CommandServer::~CommandServer() {
  server_->Shutdown(std::chrono::system_clock::now() + kShutdownGrace);
  server_->Wait();
  std::error_code ignored;
  std::filesystem::remove(socket_, ignored);
}

}  // namespace ics::camd
