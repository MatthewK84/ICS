// ics-time-align (ICS-026, deploy/sitl): fits each vehicle's boot clock to UTC
// per sortie from the SYSTEM_TIME messages in a TAP capture, and writes one
// JSON object per line on stdout:
//
//   {"kind":"sortie","system":1,"sortie":0,"samples":42,"positions":420,"fitted":true,
//    "used":42,"rejected":0,"drift_ppm":-0.03,"residual_rms_ns":2900,"residual_max_ns":9000,...}
//   {"kind":"latency","system":1,"count":420,"min_ns":...,"median_ns":...,"p95_ns":...,"max_ns":...}
//   {"kind":"log","log":0,"system":1,"sortie":0,"samples":42,"fitted":true,...}
//
// Usage: ics-time-align PCAP LATITUDE LONGITUDE HEIGHT [--log FILE]...
//                       [--inject PPM:OFFSET_MS [--withhold FROM:TO]] [--records]
//
// LATITUDE, LONGITUDE and HEIGHT (metres above the ellipsoid) are the range ENU
// frame's origin, as for ics-mavlink-replay. Each --log is an onboard log of a
// vehicle in the capture, fitted with the sortie whose boot times it overlaps:
// its own GNSS times and the capture's SYSTEM_TIME pairs together. --records
// also writes every record, and each log's records and events, timed by its
// sortie's fit ("record", "log_record" and "log_event" lines).
//
// --inject is the "Done when" check: it puts PPM of drift and OFFSET_MS of
// offset on every sortie's boot times, withholds the samples from FROM to TO
// of each sortie's span, and fits and times each position again ("check"
// lines). It exits 1 unless at least one sortie was checked and every one
// checked is within 1 ms. The geoid grid is read from where the ICS images
// install it. Exits 1 when a file cannot be read, and 2 for bad arguments.

#include <algorithm>
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
#include <vector>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/timealign/check.hpp"
#include "ics/timealign/clock_fit.hpp"
#include "output.hpp"
#include "sorties.hpp"

namespace {

namespace ta = ics::timealign;
using ics::time_align::Vehicles;

constexpr std::size_t kFirstOption = 5;

struct Plan {
  std::string capture;
  ics::frames::Geodetic origin;
  std::vector<std::string> logs;
  std::optional<ta::Injection> injection;
  ta::Withholding withholding{};
  bool records = false;
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

// A pair A:B of numbers.
template <typename A, typename B>
[[nodiscard]] std::optional<std::pair<A, B>> pair(const std::string_view text) noexcept {
  const std::size_t colon = text.find(':');
  const std::optional<A> first = colon == std::string_view::npos ? std::nullopt : number<A>(text.substr(0, colon));
  const std::optional<B> second = first ? number<B>(text.substr(colon + 1)) : std::nullopt;
  if (!second) {
    return std::nullopt;
  }
  return std::pair<A, B>{*first, *second};
}

// Takes one option, and its value if it has one; false for a bad one.
[[nodiscard]] bool take_option(const std::span<const char* const> args, std::size_t& at, Plan& plan) {
  const std::string_view option = args[at];
  const bool has_value = at + 1 < args.size();
  if (option == "--records") {
    plan.records = true;
    return true;
  }
  if (!has_value) {
    return false;
  }
  const std::string_view value = args[++at];
  if (option == "--log") {
    plan.logs.emplace_back(value);
    return true;
  }
  if (option == "--inject") {
    const auto drift = pair<double, std::int64_t>(value);
    plan.injection = drift ? std::optional(ta::Injection{.ppm = drift->first,
                                                         .offset = std::chrono::milliseconds(drift->second)})
                           : std::nullopt;
    return plan.injection.has_value();
  }
  const auto span = option == "--withhold" ? pair<double, double>(value) : std::nullopt;
  plan.withholding = span ? ta::Withholding{.from = span->first, .to = span->second} : ta::Withholding{};
  return span.has_value();
}

[[nodiscard]] std::optional<Plan> parse(const std::span<const char* const> args) {
  if (args.size() < kFirstOption) {
    return std::nullopt;
  }
  const std::optional<double> latitude = number<double>(args[2]);
  const std::optional<double> longitude = number<double>(args[3]);
  const std::optional<double> height = number<double>(args[4]);
  const ics::Result<ics::frames::Geodetic> origin =
      latitude && longitude && height
          ? ics::frames::Geodetic::make(ics::Degrees(*latitude), ics::Degrees(*longitude), ics::Meters(*height))
          : ics::fail(ics::Error::kInvalidArgument);
  if (!origin) {
    return std::nullopt;
  }
  Plan plan{.capture = args[1], .origin = *origin, .logs = {}, .injection = std::nullopt};
  for (std::size_t at = kFirstOption; at < args.size(); ++at) {
    if (!take_option(args, at, plan)) {
      return std::nullopt;
    }
  }
  return plan;
}

int align(const Plan& plan) {
  const ics::Result<ics::frames::Egm96> geoid = ics::frames::Egm96::load(ics::frames::Egm96::kDefaultPath);
  if (!geoid) {
    std::fputs("cannot read the EGM96 grid\n", stderr);
    return 1;
  }
  const ics::frames::EnuFrame range(plan.origin);
  std::string reason;
  const ics::Result<Vehicles> vehicles = ics::time_align::collect(plan.capture, *geoid, range, reason);
  if (!vehicles) {
    std::fprintf(stderr, "%s\n", reason.c_str());
    return 1;
  }
  ics::time_align::report(*vehicles, plan.records);
  for (std::size_t index = 0; index < plan.logs.size(); ++index) {
    if (!ics::time_align::report_log(index, plan.logs[index], *vehicles, *geoid, range, plan.records)) {
      std::fprintf(stderr, "cannot import %s\n", plan.logs[index].c_str());
      return 1;
    }
  }
  if (!plan.injection) {
    return 0;
  }
  return ics::time_align::check(*vehicles, *plan.injection, plan.withholding) ? 0 : 1;
}

}  // namespace

int main(const int argc, char* argv[]) {
  const std::optional<Plan> plan = parse(std::span<const char* const>(argv, static_cast<std::size_t>(argc)));
  if (!plan) {
    std::fputs(
        "usage: ics-time-align PCAP LATITUDE LONGITUDE HEIGHT [--log FILE]... "
        "[--inject PPM:OFFSET_MS [--withhold FROM:TO]] [--records]\n",
        stderr);
    return 2;
  }
  return align(*plan);
}
