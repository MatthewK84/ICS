#include "ics/plid/query_client.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <google/protobuf/util/json_util.h>
#include <sys/socket.h>

#include "ics/common/check.hpp"
#include "ics/plid/run.hpp"
#include "ics/timing/unix_socket.hpp"
#include "ics/v1/pli_query.pb.h"

namespace ics::plid {
namespace {

constexpr std::size_t kKindAt = 1;
constexpr std::size_t kRangeArguments = 4;
constexpr std::size_t kMaxArguments = 5;
// Larger than any response the server sends.
constexpr std::size_t kResponseBytes = std::size_t{1} << 22U;

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

// A connected client socket; invalid when nothing answers at path.
[[nodiscard]] timing::Fd connect(const std::filesystem::path& path) {
  timing::Fd socket(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
  const Result<sockaddr_un> address = timing::unix_address(path);
  const bool connected =
      address && ::connect(socket.get(), reinterpret_cast<const sockaddr*>(&*address), sizeof(sockaddr_un)) == 0;
  return connected ? std::move(socket) : timing::Fd();
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

// Reads responses until the last; nothing when the connection ends first.
[[nodiscard]] std::optional<v1::QueryPliResponse> read_all(const int socket, std::FILE* out, std::uint64_t& count) {
  std::vector<char> buffer(kResponseBytes);
  for (bool open = true; open;) {
    const ssize_t got = ::recv(socket, buffer.data(), buffer.size(), 0);
    v1::QueryPliResponse response;
    open = got > 0 && response.ParseFromArray(buffer.data(), static_cast<int>(got));
    count += open ? print(response, out) : 0;
    if (open && response.done()) {
      return response;
    }
  }
  return std::nullopt;
}

}  // namespace

int run_query_client(const std::span<const char* const> args, const std::filesystem::path& socket, std::FILE* out) {
  const std::optional<v1::QueryPliRequest> request = request_of(args);
  if (!request) {
    std::fputs("usage: ics-pli-query records|events [START_UTC_NS END_UTC_NS [ENTITY]]\n", stderr);
    return kExitUsage;
  }
  const timing::Fd client = connect(socket);
  const std::string bytes = request->SerializeAsString();
  const bool sent = ::send(client.get(), bytes.data(), bytes.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(bytes.size());
  std::uint64_t count = 0;
  const std::optional<v1::QueryPliResponse> last = sent ? read_all(client.get(), out, count) : std::nullopt;
  if (!last) {
    std::fprintf(stderr, "no answer from %s\n", socket.c_str());
    return kExitFailed;
  }
  static_cast<void>(ics::check(last->done()));
  std::fprintf(out, R"({"kind":"done","count":%llu,"truncated":%s})" "\n", static_cast<unsigned long long>(count),
               last->truncated() ? "true" : "false");
  if (!last->error().empty()) {
    std::fprintf(stderr, "the query failed: %s\n", last->error().c_str());
  }
  return last->error().empty() && !last->truncated() ? kExitStopped : kExitFailed;
}

}  // namespace ics::plid
