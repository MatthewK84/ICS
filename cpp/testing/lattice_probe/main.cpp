// ics-lattice-probe (ICS-023): streams entities from Lattice through the
// Lattice adapter for a while and writes each record on stdout, one
// ics.v1.PliRecord per line in protobuf's JSON form, with proto field names
// and zeros written out. Counts go to stderr at the end.
//
// Usage: ics-lattice-probe URL TOKEN_FILE SECONDS MIN_RECORDS LATITUDE LONGITUDE HEIGHT
//                          [SANDBOX_TOKEN_FILE]
//
// URL is the Lattice environment's base URL. TOKEN_FILE holds its token and
// SANDBOX_TOKEN_FILE, for a Lattice Sandbox, the sandbox token; only their
// owner may have access to them. LATITUDE, LONGITUDE and HEIGHT (metres above
// the ellipsoid) are the range ENU frame's origin. Exits 0 once it has
// streamed for SECONDS and written at least MIN_RECORDS records, 3 when it
// wrote fewer, 1 when a token file cannot be used or Lattice refused the
// request, and 2 for bad arguments.

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <google/protobuf/util/json_util.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/lattice/adapter.hpp"
#include "ics/lattice/event.hpp"
#include "ics/lattice/stream_client.hpp"
#include "ics/lattice/token.hpp"

namespace {

constexpr std::size_t kArguments = 8;
constexpr std::size_t kWithSandbox = 9;
constexpr int kRefused = 1;
constexpr int kUsage = 2;
constexpr int kTooFew = 3;
constexpr auto kTimerStep = std::chrono::milliseconds(100);

struct Plan {
  std::string_view url;
  std::string_view token_file;
  std::string_view sandbox_token_file;
  std::chrono::seconds length{};
  std::uint64_t min_records = 0;
  ics::frames::Geodetic origin;
};

template <typename T>
[[nodiscard]] std::optional<T> number(const std::string_view text) noexcept {
  T out{};
  const std::from_chars_result read = std::from_chars(text.begin(), text.end(), out);
  if (read.ec != std::errc() || read.ptr != text.end() || text.empty()) {
    return std::nullopt;
  }
  return out;
}

[[nodiscard]] std::optional<Plan> parse(const std::span<const char* const> args) {
  if (args.size() != kArguments && args.size() != kWithSandbox) {
    return std::nullopt;
  }
  const std::optional<std::uint32_t> seconds = number<std::uint32_t>(args[3]);
  const std::optional<std::uint64_t> min_records = number<std::uint64_t>(args[4]);
  const std::optional<double> latitude = number<double>(args[5]);
  const std::optional<double> longitude = number<double>(args[6]);
  const std::optional<double> height = number<double>(args[7]);
  const ics::Result<ics::frames::Geodetic> origin =
      latitude && longitude && height
          ? ics::frames::Geodetic::make(ics::Degrees(*latitude), ics::Degrees(*longitude), ics::Meters(*height))
          : ics::fail(ics::Error::kInvalidArgument);
  if (!seconds || !min_records || !origin) {
    return std::nullopt;
  }
  return Plan{.url = args[1],
              .token_file = args[2],
              .sandbox_token_file = args.size() == kWithSandbox ? std::string_view(args[8]) : std::string_view{},
              .length = std::chrono::seconds(*seconds),
              .min_records = *min_records,
              .origin = *origin};
}

// Prints each record the adapter makes from an event.
class Printer final : public ics::lattice::EventSink {
 public:
  explicit Printer(const ics::lattice::Adapter& adapter) : adapter_(adapter) {}

  void accept(const ics::lattice::Event& event, const ics::UtcTime received) override {
    const std::optional<ics::v1::PliRecord> record = adapter_.record(event, received);
    std::string text;
    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    options.always_print_fields_with_no_presence = true;
    if (record && google::protobuf::util::MessageToJsonString(*record, &text, options).ok()) {
      std::printf("%s\n", text.c_str());
      std::fflush(stdout);
      ++records_;
    }
  }

  [[nodiscard]] std::uint64_t records() const noexcept { return records_; }

 private:
  const ics::lattice::Adapter& adapter_;
  std::uint64_t records_ = 0;
};

// The client's settings, with the tokens read from their files.
[[nodiscard]] ics::Result<ics::lattice::ClientSettings> client_settings(const Plan& plan) {
  const ics::Result<std::string> token = ics::lattice::read_token(plan.token_file);
  const ics::Result<std::string> sandbox_token = plan.sandbox_token_file.empty()
                                                     ? ics::Result<std::string>(std::string())
                                                     : ics::lattice::read_token(plan.sandbox_token_file);
  if (!token || !sandbox_token) {
    std::fprintf(stderr, "ics-lattice-probe: a token file is missing, shared with other users or holds no token\n");
    return ics::fail(ics::Error::kUnreadable);
  }
  ics::lattice::ClientSettings settings;
  settings.url = plan.url;
  settings.token = *token;
  settings.sandbox_token = *sandbox_token;
  return settings;
}

void report(const ics::lattice::ClientStats& stats, const std::uint64_t records) {
  std::fprintf(stderr,
               "connections %llu, events %llu, malformed %llu, oversized %llu, records %llu; last HTTP status "
               "%lld, last error: %s\n",
               static_cast<unsigned long long>(stats.connections), static_cast<unsigned long long>(stats.events),
               static_cast<unsigned long long>(stats.malformed), static_cast<unsigned long long>(stats.oversized),
               static_cast<unsigned long long>(records), static_cast<long long>(stats.last_http_status),
               stats.last_error.c_str());
}

// Streams for the plan's length, or until Lattice refuses the request.
[[nodiscard]] int stream(const Plan& plan, ics::lattice::StreamClient& client) {
  const ics::lattice::Adapter adapter = ics::lattice::Adapter::make({}, ics::frames::EnuFrame(plan.origin)).value();
  Printer printer(adapter);
  std::atomic<bool> stop{false};
  std::thread timer([&stop, length = plan.length] {
    const auto until = std::chrono::steady_clock::now() + length;
    while (!stop.load() && std::chrono::steady_clock::now() < until) {
      std::this_thread::sleep_for(kTimerStep);
    }
    stop.store(true);
  });
  const ics::Status ran = client.run(printer, stop);
  stop.store(true);
  timer.join();
  report(client.stats(), printer.records());
  if (!ran) {
    return kRefused;
  }
  return printer.records() >= plan.min_records ? 0 : kTooFew;
}

}  // namespace

int main(const int argc, const char* const* const argv) {
  const std::optional<Plan> plan = parse(std::span(argv, static_cast<std::size_t>(argc)));
  if (!plan) {
    std::fprintf(stderr,
                 "usage: ics-lattice-probe URL TOKEN_FILE SECONDS MIN_RECORDS LATITUDE LONGITUDE HEIGHT "
                 "[SANDBOX_TOKEN_FILE]\n");
    return kUsage;
  }
  const ics::Result<ics::lattice::ClientSettings> settings = client_settings(*plan);
  if (!settings) {
    return kRefused;
  }
  ics::Result<ics::lattice::StreamClient> client = ics::lattice::StreamClient::make(*settings);
  if (!client) {
    std::fprintf(stderr, "ics-lattice-probe: the URL is not HTTPS, or the tokens are not valid\n");
    return kUsage;
  }
  return stream(*plan, *client);
}
