#include "ics/mount/pwi4_client.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <curl/curl.h>

#include "ics/common/check.hpp"

namespace ics::mount {

struct Pwi4Client::Connection {
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle{curl_easy_init(), &curl_easy_cleanup};
  std::array<char, CURL_ERROR_SIZE> error{};
};

namespace {

using std::chrono::milliseconds;
using std::chrono::steady_clock;

constexpr long kHttpOk = 200;
// PWI4 on this machine: plain HTTP never leaves it.
constexpr std::string_view kLocalPwi4 = "http://127.0.0.1:";
constexpr double kArcsecPerRadian = 180.0 * 3600.0 / std::numbers::pi;
constexpr double kDegreesPerTurn = 360.0;

// Appends what libcurl reads to the body, refusing more than kMaxStatusBytes.
std::size_t on_data(const char* data, const std::size_t size, const std::size_t count, std::string* body) {
  const std::size_t bytes = size * count;
  if (body->size() + bytes > kMaxStatusBytes) {
    return 0;
  }
  body->append(data, bytes);
  return bytes;
}

// A number as a query value, as short as reads back exactly.
[[nodiscard]] std::string number(const double value) {
  std::array<char, 32> text{};
  const std::to_chars_result written = std::to_chars(text.begin(), text.end(), value);
  // 32 characters hold any double.
  static_cast<void>(ics::check(written.ec == std::errc()));
  return {text.data(), written.ptr};
}

[[nodiscard]] double arcsec_per_s(const double rad_per_s) noexcept { return rad_per_s * kArcsecPerRadian; }

[[nodiscard]] bool valid(const Pwi4Settings& settings) noexcept {
  return settings.port != 0 && settings.retry.attempts > 0 &&
         settings.timeout > milliseconds::zero();
}

[[nodiscard]] Status done(const Result<std::string>& answer) {
  if (!answer) {
    return fail(answer.error());
  }
  return {};
}

}  // namespace

Pwi4Client::Pwi4Client(const Pwi4Settings& settings, std::unique_ptr<Connection> connection) noexcept
    : settings_(settings),
      base_url_(std::string(kLocalPwi4) + std::to_string(settings_.port)),
      connection_(std::move(connection)) {}

Pwi4Client::~Pwi4Client() = default;
Pwi4Client::Pwi4Client(Pwi4Client&& other) noexcept = default;
Pwi4Client& Pwi4Client::operator=(Pwi4Client&& other) noexcept = default;

Result<Pwi4Client> Pwi4Client::make(const Pwi4Settings& settings) {
  static const bool initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
  static_cast<void>(ics::check(initialized));
  if (!valid(settings)) {
    return fail(Error::kInvalidArgument);
  }
  auto connection = std::make_unique<Connection>();
  // libcurl makes a handle unless memory runs out.
  static_cast<void>(ics::check(connection->handle != nullptr));
  CURL* const handle = connection->handle.get();
  const std::array set{
      curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L),
      curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L),
      curl_easy_setopt(handle, CURLOPT_TCP_NODELAY, 1L),
      curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &on_data),
      curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, connection->error.data()),
  };
  for (const CURLcode code : set) {
    static_cast<void>(ics::check(code == CURLE_OK));
  }
  return Pwi4Client(settings, std::move(connection));
}

Result<MountStatus> Pwi4Client::status(const Deadline deadline) {
  const Result<std::string> answer = request("/status", deadline);
  if (!answer) {
    return fail(answer.error());
  }
  return parse_status(*answer);
}

Status Pwi4Client::connect(const Deadline deadline) { return done(request("/mount/connect", deadline)); }

Status Pwi4Client::enable(const Axis axis, const Deadline deadline) {
  return done(request("/mount/enable?axis=" + std::to_string(static_cast<int>(axis)), deadline));
}

Status Pwi4Client::goto_alt_az(const AltAz& target, const Deadline deadline) {
  const Result<MountStatus> now = status(deadline);
  const Status allowed = now ? check_goto(target, *now, settings_.limits) : fail(now.error());
  if (!allowed) {
    return allowed;
  }
  const double azimuth = std::fmod(to_degrees(target.azimuth).value(), kDegreesPerTurn);
  const double azimuth_0_360 = azimuth < 0.0 ? azimuth + kDegreesPerTurn : azimuth;
  return done(request("/mount/goto_alt_az?alt_degs=" + number(to_degrees(target.altitude).value()) +
                          "&az_degs=" + number(azimuth_0_360),
                      deadline));
}

Result<AppliedRates> Pwi4Client::set_rates(const AxisRates& demand, const MountStatus& status,
                                           const Deadline deadline) {
  const AppliedRates applied = limit_rates(demand, status, settings_.limits);
  const Result<std::string> answer =
      request("/mount/offset?axis0_set_rate_arcsec_per_sec=" + number(arcsec_per_s(applied.rates.rad_per_s[0])) +
                  "&axis1_set_rate_arcsec_per_sec=" + number(arcsec_per_s(applied.rates.rad_per_s[1])),
              deadline);
  if (!answer) {
    return fail(answer.error());
  }
  return applied;
}

Status Pwi4Client::stop_rates(const Deadline deadline) {
  return done(request("/mount/offset?axis0_stop_rate=0&axis1_stop_rate=0", deadline));
}

Status Pwi4Client::stop(const Deadline deadline) { return done(request("/mount/stop", deadline)); }

Status Pwi4Client::park(const Deadline deadline) { return done(request("/mount/park", deadline)); }

Result<std::string> Pwi4Client::request(const std::string& path, const Deadline deadline) {
  ++stats_.requests;
  const std::string url = base_url_ + path;
  const CURLcode set = curl_easy_setopt(connection_->handle.get(), CURLOPT_URL, url.c_str());
  static_cast<void>(ics::check(set == CURLE_OK));
  milliseconds backoff = settings_.retry.first_backoff;
  for (std::uint32_t i = 0; i < settings_.retry.attempts; ++i) {
    if (i > 0 && steady_clock::now() + backoff >= deadline) {
      break;
    }
    if (i > 0) {
      std::this_thread::sleep_for(backoff);
      backoff *= 2;
    }
    std::string body;
    if (attempt(deadline, body)) {
      return body;
    }
  }
  ++stats_.failures;
  return fail(Error::kUnavailable);
}

bool Pwi4Client::attempt(const Deadline deadline, std::string& body) {
  const auto left = std::chrono::duration_cast<milliseconds>(deadline - steady_clock::now());
  if (left <= milliseconds::zero()) {
    stats_.last_error = "deadline passed";
    return false;
  }
  ++stats_.attempts;
  CURL* const handle = connection_->handle.get();
  connection_->error[0] = '\0';
  const long timeout_ms = std::min(left, settings_.timeout).count();
  const std::array set{curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout_ms),
                       curl_easy_setopt(handle, CURLOPT_WRITEDATA, &body)};
  for (const CURLcode set_code : set) {
    static_cast<void>(ics::check(set_code == CURLE_OK));
  }
  const CURLcode code = curl_easy_perform(handle);
  long http = 0;
  static_cast<void>(curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http));
  static_cast<void>(curl_easy_setopt(handle, CURLOPT_WRITEDATA, nullptr));
  if (code != CURLE_OK || http != kHttpOk) {
    stats_.last_error = code != CURLE_OK ? std::string(curl_easy_strerror(code)) + ": " + connection_->error.data()
                                         : "HTTP " + std::to_string(http);
    return false;
  }
  return true;
}

}  // namespace ics::mount
