#include "ics/camd/command_client.hpp"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include <google/protobuf/util/json_util.h>
#include <grpcpp/grpcpp.h>

#include "ics/camd/run.hpp"
#include "ics/v1/camera_service.grpc.pb.h"

namespace ics::camd {
namespace {

constexpr std::size_t kRunIdAt = 1;
constexpr std::size_t kSocketAt = 2;
constexpr std::size_t kMinArguments = 2;
constexpr std::size_t kMaxArguments = 3;

// Which run to record, and where ics-camd listens.
struct Command {
  std::string run_id;
  std::filesystem::path socket;
};

// The command the arguments ask for, or nothing for bad ones.
[[nodiscard]] std::optional<Command> command_of(const std::span<const char* const> args) {
  if (args.size() < kMinArguments || args.size() > kMaxArguments) {
    return std::nullopt;
  }
  return Command{.run_id = args[kRunIdAt], .socket = args.size() > kSocketAt ? args[kSocketAt] : kCommandSocket};
}

[[nodiscard]] std::string json(const google::protobuf::Message& message) {
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  options.always_print_fields_with_no_presence = true;
  std::string out;
  static_cast<void>(google::protobuf::util::MessageToJsonString(message, &out, options));
  return out;
}

// Writes each camera's line, and whether every camera verified; a camera
// that did not is named on stderr.
[[nodiscard]] bool report(const v1::ArmAndOffloadResponse& response, std::FILE* out) {
  bool verified = true;
  for (const v1::CameraOffload& camera : response.cameras()) {
    std::fprintf(out, "%s\n", json(camera).c_str());
    if (!camera.verified()) {
      std::fprintf(stderr, "ics-cam-offload: %s did not verify%s%s\n", camera.camera_id().c_str(),
                   camera.error().empty() ? "" : ": ", camera.error().c_str());
      verified = false;
    }
  }
  return verified;
}

}  // namespace

int run_offload_client(const std::span<const char* const> args, std::FILE* out) {
  const std::optional<Command> command = command_of(args);
  if (!command) {
    std::fputs("usage: ics-cam-offload RUN_ID [SOCKET]\n", stderr);
    return kExitUsage;
  }
  const std::unique_ptr<v1::CameraService::Stub> stub = v1::CameraService::NewStub(
      grpc::CreateChannel("unix:" + command->socket.native(), grpc::InsecureChannelCredentials()));
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + kCommandDeadline);
  v1::ArmAndOffloadRequest request;
  request.set_run_id(command->run_id);
  v1::ArmAndOffloadResponse response;
  const grpc::Status status = stub->ArmAndOffload(&context, request, &response);
  if (!status.ok()) {
    std::fprintf(stderr, "ics-cam-offload: %s: %s\n", command->socket.c_str(), status.error_message().c_str());
    return kExitUnavailable;
  }
  return report(response, out) ? kExitStopped : kExitUnavailable;
}

}  // namespace ics::camd
