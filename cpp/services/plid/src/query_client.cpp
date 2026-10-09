#include "ics/plid/query_client.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <google/protobuf/util/json_util.h>
#include <grpcpp/grpcpp.h>

#include "ics/common/check.hpp"
#include "ics/plid/run.hpp"
#include "ics/v1/pli_query.grpc.pb.h"

namespace ics::plid {
namespace {

constexpr std::size_t kKindAt = 1;
constexpr std::size_t kRangeArguments = 4;
constexpr std::size_t kMaxArguments = 5;
// How long one query may take, all its batches read, before it is given up.
constexpr std::chrono::seconds kQueryTimeout{60};

// What the stream of a query brought.
struct Outcome {
  std::uint64_t count = 0;
  bool done = false;
  bool truncated = false;
};

[[nodiscard]] std::optional<std::int64_t> number(const std::string_view text) noexcept {
  std::int64_t out = 0;
  const std::from_chars_result read = std::from_chars(text.begin(), text.end(), out);
  return read.ec == std::errc() && read.ptr == text.end() ? std::optional(out) : std::nullopt;
}

// The request the arguments ask for, or nothing for bad ones.
[[nodiscard]] std::optional<v1::QueryPliRequest> request_of(const std::span<const char* const> args) {
  const std::string_view kind = args.size() > kKindAt ? args[kKindAt] : "";
  const bool sized = args.size() == kKindAt + 1 || args.size() == kRangeArguments || args.size() == kMaxArguments;
  const std::optional<std::int64_t> start =
      args.size() >= kRangeArguments ? number(args[2]) : std::numeric_limits<std::int64_t>::min();
  const std::optional<std::int64_t> end =
      args.size() >= kRangeArguments ? number(args[3]) : std::numeric_limits<std::int64_t>::max();
  if (!sized || (kind != "records" && kind != "events") || !start || !end) {
    return std::nullopt;
  }
  v1::QueryPliRequest out;
  out.set_kind(kind == "records" ? v1::QueryPliRequest::KIND_RECORDS : v1::QueryPliRequest::KIND_EVENTS);
  out.set_start_utc_ns(*start);
  out.set_end_utc_ns(*end);
  out.set_entity_id(args.size() == kMaxArguments ? args[4] : "");
  return out;
}

[[nodiscard]] std::string json(const google::protobuf::Message& message) {
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  options.always_print_fields_with_no_presence = true;
  std::string out;
  static_cast<void>(google::protobuf::util::MessageToJsonString(message, &out, options));
  return out;
}

// Writes each record and event of a response; returns how many.
std::uint64_t print(const v1::QueryPliResponse& response, std::FILE* out) {
  for (const v1::PliRecord& record : response.records()) {
    std::fprintf(out, R"({"kind":"record","record":%s})" "\n", json(record).c_str());
  }
  for (const v1::PliEvent& event : response.events()) {
    std::fprintf(out, R"({"kind":"event","event":%s})" "\n", json(event).c_str());
  }
  return static_cast<std::uint64_t>(response.records_size()) + static_cast<std::uint64_t>(response.events_size());
}

// Asks the server at socket and writes each batch's records and events as it
// comes; returns the status the stream ended with.
[[nodiscard]] grpc::Status read_all(const std::filesystem::path& socket, const v1::QueryPliRequest& request,
                                    std::FILE* out, Outcome& outcome) {
  const std::unique_ptr<v1::PliQueryService::Stub> stub =
      v1::PliQueryService::NewStub(grpc::CreateChannel("unix:" + socket.native(), grpc::InsecureChannelCredentials()));
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + kQueryTimeout);
  const std::unique_ptr<grpc::ClientReader<v1::QueryPliResponse>> reader = stub->QueryPli(&context, request);
  v1::QueryPliResponse response;
  while (reader->Read(&response)) {
    outcome.count += print(response, out);
    outcome.done = response.done();
    outcome.truncated = response.truncated();
  }
  return reader->Finish();
}

}  // namespace

int run_query_client(const std::span<const char* const> args, const std::filesystem::path& socket, std::FILE* out) {
  const std::optional<v1::QueryPliRequest> request = request_of(args);
  if (!request) {
    std::fputs("usage: ics-pli-query records|events [START_UTC_NS END_UTC_NS [ENTITY]]\n", stderr);
    return kExitUsage;
  }
  Outcome outcome;
  const grpc::Status status = read_all(socket, *request, out, outcome);
  if (status.error_code() == grpc::StatusCode::INVALID_ARGUMENT) {
    std::fprintf(stderr, "the query failed: %s\n", status.error_message().c_str());
    return kExitFailed;
  }
  if (!status.ok()) {
    std::fprintf(stderr, "no answer from %s: %s\n", socket.c_str(), status.error_message().c_str());
    return kExitFailed;
  }
  // The server ends every query it answers with a batch that has done set.
  static_cast<void>(ics::check(outcome.done));
  std::fprintf(out, R"({"kind":"done","count":%llu,"truncated":%s})" "\n",
               static_cast<unsigned long long>(outcome.count), outcome.truncated ? "true" : "false");
  return outcome.truncated ? kExitFailed : kExitStopped;
}

}  // namespace ics::plid
