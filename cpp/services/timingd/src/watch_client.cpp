#include "ics/timingd/watch_client.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <google/protobuf/util/json_util.h>
#include <grpcpp/grpcpp.h>

#include "ics/timingd/run.hpp"
#include "ics/v1/time_quality_service.grpc.pb.h"

namespace ics::timingd {
namespace {

constexpr std::size_t kSocketAt = 1;
constexpr std::size_t kCountAt = 2;
constexpr std::size_t kMaxArguments = 3;

// What to watch, and how many reports to stop after; 0 for no limit.
struct Watch {
  std::filesystem::path socket;
  std::uint64_t count = 0;
};

// The watch the arguments ask for, or nothing for bad ones.
[[nodiscard]] std::optional<Watch> watch_of(const std::span<const char* const> args) {
  if (args.size() > kMaxArguments) {
    return std::nullopt;
  }
  Watch watch{.socket = args.size() > kSocketAt ? args[kSocketAt] : kReportSocket, .count = 0};
  if (args.size() <= kCountAt) {
    return watch;
  }
  const std::string_view text = args[kCountAt];
  const std::from_chars_result read = std::from_chars(text.begin(), text.end(), watch.count);
  if (read.ec != std::errc() || read.ptr != text.end() || watch.count == 0) {
    return std::nullopt;
  }
  return watch;
}

[[nodiscard]] std::string json(const google::protobuf::Message& message) {
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  options.always_print_fields_with_no_presence = true;
  std::string out;
  static_cast<void>(google::protobuf::util::MessageToJsonString(message, &out, options));
  return out;
}

// Writes each report as it comes, until the stream ends or watch.count are
// written; returns the status the stream ended with, OK when it was stopped
// at watch.count.
[[nodiscard]] grpc::Status read_reports(const Watch& watch, std::FILE* out) {
  const std::unique_ptr<v1::TimeQualityService::Stub> stub = v1::TimeQualityService::NewStub(
      grpc::CreateChannel("unix:" + watch.socket.native(), grpc::InsecureChannelCredentials()));
  grpc::ClientContext context;
  const std::unique_ptr<grpc::ClientReader<v1::WatchTimeQualityResponse>> reader =
      stub->WatchTimeQuality(&context, v1::WatchTimeQualityRequest());
  v1::WatchTimeQualityResponse response;
  std::uint64_t written = 0;
  while ((watch.count == 0 || written < watch.count) && reader->Read(&response)) {
    std::fprintf(out, "%s\n", json(response.report()).c_str());
    // Each line as it comes: a reader of the file sees the report at once.
    std::fflush(out);
    ++written;
  }
  const bool enough = watch.count != 0 && written == watch.count;
  if (enough) {
    context.TryCancel();
  }
  const grpc::Status status = reader->Finish();
  return enough ? grpc::Status::OK : status;
}

}  // namespace

int run_watch_client(const std::span<const char* const> args, std::FILE* out) {
  const std::optional<Watch> watch = watch_of(args);
  if (!watch) {
    std::fputs("usage: ics-time-watch [SOCKET [COUNT]]\n", stderr);
    return kExitUsage;
  }
  const grpc::Status status = read_reports(*watch, out);
  if (!status.ok()) {
    std::fprintf(stderr, "ics-time-watch: %s: %s\n", watch->socket.c_str(), status.error_message().c_str());
    return kExitUnavailable;
  }
  return kExitStopped;
}

}  // namespace ics::timingd
