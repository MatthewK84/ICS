#include "ics/lattice/stream_client.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <curl/curl.h>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/lattice/event.hpp"
#include "ics/lattice/sse.hpp"
#include "ics/lattice/token.hpp"

namespace ics::lattice {
namespace {

using HeaderList = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

}  // namespace

struct StreamClient::Connection {
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle{curl_easy_init(), &curl_easy_cleanup};
  HeaderList headers{nullptr, &curl_slist_free_all};
  std::array<char, CURL_ERROR_SIZE> error{};
};

namespace {

constexpr std::string_view kStreamPath = "/api/v1/entities/stream";
// HTTPS, or plain HTTP to this host only.
constexpr std::array<std::string_view, 4> kSchemes{"https://", "http://127.0.0.1:", "http://localhost:",
                                                   "http://[::1]:"};
// The stream is lost after this many heartbeat intervals without a byte.
constexpr long kSilentHeartbeats = 3;
constexpr auto kPauseStep = std::chrono::milliseconds(10);
// HTTP statuses worth trying again: a timeout, too many requests, and every
// server error. libcurl reports every status from 400 as an error.
constexpr long kRequestTimeout = 408;
constexpr long kTooManyRequests = 429;
constexpr long kFirstServerError = 500;

// One connection's state, which libcurl's callbacks see.
struct Attempt {
  EventSink& sink;
  ClientStats& stats;
  const std::atomic<bool>& stop;
  SseReader reader;
  bool delivered = false;
};

[[nodiscard]] bool valid_url(const std::string_view url) noexcept {
  return valid_token(url) && std::ranges::any_of(kSchemes, [url](const std::string_view scheme) {
           return url.starts_with(scheme);
         });
}

[[nodiscard]] bool valid_component(const std::string& name) noexcept {
  return !name.empty() && std::ranges::all_of(name, [](const char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  });
}

[[nodiscard]] bool valid(const ClientSettings& settings) noexcept {
  const bool tokens = valid_token(settings.token) &&
                      (settings.sandbox_token.empty() || valid_token(settings.sandbox_token));
  const bool times = settings.heartbeat_interval >= std::chrono::seconds(1) &&
                     settings.connect_timeout >= std::chrono::seconds(1) &&
                     settings.first_backoff > Duration::zero() && settings.max_backoff >= settings.first_backoff;
  return valid_url(settings.url) && tokens && times && std::ranges::all_of(settings.components, valid_component);
}

// The JSON request: a heartbeat every interval, every entity and then every
// change, and the components asked for.
[[nodiscard]] std::string request_body(const ClientSettings& settings) {
  google::protobuf::Struct body;
  auto& fields = *body.mutable_fields();
  const auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(settings.heartbeat_interval);
  fields["heartbeatIntervalMS"].set_number_value(static_cast<double>(interval.count()));
  fields["preExistingOnly"].set_bool_value(false);
  google::protobuf::ListValue& components = *fields["componentsToInclude"].mutable_list_value();
  for (const std::string& name : settings.components) {
    components.add_values()->set_string_value(name);
  }
  std::string out;
  static_cast<void>(check(google::protobuf::util::MessageToJsonString(body, &out).ok()));
  return out;
}

void add_header(HeaderList& headers, const std::string& header) {
  curl_slist* const head = curl_slist_append(headers.get(), header.c_str());
  static_cast<void>(check(head != nullptr));
  if (headers == nullptr) {
    headers.reset(head);
  }
}

[[nodiscard]] UtcTime now() {
  return std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now());
}

// libcurl's write callback: the stream's next bytes.
std::size_t on_data(char* data, const std::size_t size, const std::size_t count, void* context) {
  Attempt& attempt = *static_cast<Attempt*>(context);
  for (const SseEvent& sse : attempt.reader.feed(std::string_view(data, size * count))) {
    const Result<std::optional<Event>> decoded = decode_event(sse.data);
    attempt.stats.malformed += decoded.has_value() ? 0U : 1U;
    if (decoded.has_value() && decoded->has_value()) {
      ++attempt.stats.events;
      attempt.delivered = true;
      attempt.sink.accept(**decoded, now());
    }
  }
  return size * count;
}

// libcurl's progress callback, called at least once a second: nonzero ends
// the transfer.
int on_progress(void* context, curl_off_t /*download_total*/, curl_off_t /*downloaded*/, curl_off_t /*upload_total*/,
                curl_off_t /*uploaded*/) {
  return static_cast<Attempt*>(context)->stop.load() ? 1 : 0;
}

// Waits for length, or until stop is set.
void pause(const Duration length, const std::atomic<bool>& stop) {
  const auto until = std::chrono::steady_clock::now() + length;
  while (!stop.load() && std::chrono::steady_clock::now() < until) {
    std::this_thread::sleep_for(kPauseStep);
  }
}

}  // namespace

StreamClient::StreamClient(ClientSettings settings, std::unique_ptr<Connection> connection) noexcept
    : settings_(std::move(settings)), connection_(std::move(connection)) {}

StreamClient::~StreamClient() = default;
StreamClient::StreamClient(StreamClient&& other) noexcept = default;
StreamClient& StreamClient::operator=(StreamClient&& other) noexcept = default;

Result<StreamClient> StreamClient::make(ClientSettings settings) {
  static const bool initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
  static_cast<void>(check(initialized));
  if (!valid(settings)) {
    return fail(Error::kInvalidArgument);
  }
  auto connection = std::make_unique<Connection>();
  add_header(connection->headers, "Authorization: Bearer " + settings.token);
  if (!settings.sandbox_token.empty()) {
    add_header(connection->headers, "Anduril-Sandbox-Authorization: Bearer " + settings.sandbox_token);
  }
  add_header(connection->headers, "Content-Type: application/json");
  add_header(connection->headers, "Accept: text/event-stream");
  std::string url = settings.url;
  while (url.ends_with('/')) {
    url.pop_back();
  }
  url.append(kStreamPath);
  CURL* const handle = connection->handle.get();
  const long silent = kSilentHeartbeats * static_cast<long>(settings.heartbeat_interval.count());
  const std::array results{
      curl_easy_setopt(handle, CURLOPT_URL, url.c_str()),
      curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https,http"),
      curl_easy_setopt(handle, CURLOPT_COPYPOSTFIELDS, request_body(settings).c_str()),
      curl_easy_setopt(handle, CURLOPT_HTTPHEADER, connection->headers.get()),
      curl_easy_setopt(handle, CURLOPT_CAINFO, settings.ca_file.empty() ? nullptr : settings.ca_file.c_str()),
      curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L),
      curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L),
      curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L),
      curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L),
      curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L),
      curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, static_cast<long>(settings.connect_timeout.count())),
      curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L),
      curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, silent),
      curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &on_data),
      curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &on_progress),
      curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L),
      curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, connection->error.data()),
  };
  static_cast<void>(check(std::ranges::all_of(results, [](const CURLcode code) { return code == CURLE_OK; })));
  return StreamClient(std::move(settings), std::move(connection));
}

Status StreamClient::run(EventSink& sink, const std::atomic<bool>& stop) {
  Duration backoff = settings_.first_backoff;
  while (!stop.load()) {
    const Outcome outcome = attempt(sink, stop);
    if (outcome == Outcome::kRefused) {
      return fail(Error::kUnavailable);
    }
    if (outcome == Outcome::kDelivered) {
      backoff = settings_.first_backoff;
    }
    pause(backoff, stop);
    backoff = std::min(backoff * 2, settings_.max_backoff);
  }
  return {};
}

StreamClient::Outcome StreamClient::attempt(EventSink& sink, const std::atomic<bool>& stop) {
  Attempt context{.sink = sink, .stats = stats_, .stop = stop, .reader = SseReader(), .delivered = false};
  CURL* const handle = connection_->handle.get();
  connection_->error.front() = '\0';
  const std::array bound{curl_easy_setopt(handle, CURLOPT_WRITEDATA, &context),
                         curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &context)};
  static_cast<void>(check(std::ranges::all_of(bound, [](const CURLcode code) { return code == CURLE_OK; })));
  ++stats_.connections;
  const CURLcode code = curl_easy_perform(handle);
  long status = 0;
  const std::array read{curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status),
                        curl_easy_setopt(handle, CURLOPT_WRITEDATA, nullptr),
                        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, nullptr)};
  static_cast<void>(check(std::ranges::all_of(read, [](const CURLcode result) { return result == CURLE_OK; })));
  stats_.last_http_status = status;
  stats_.last_error = std::string(curl_easy_strerror(code)) + ": " + connection_->error.data();
  stats_.oversized += context.reader.oversized();
  const bool refused = code == CURLE_HTTP_RETURNED_ERROR && status < kFirstServerError &&
                       status != kRequestTimeout && status != kTooManyRequests;
  if (refused) {
    return Outcome::kRefused;
  }
  return context.delivered ? Outcome::kDelivered : Outcome::kFailed;
}

}  // namespace ics::lattice
