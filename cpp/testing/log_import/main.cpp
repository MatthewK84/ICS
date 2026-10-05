// ics-log-import (ICS-025, deploy/sitl): runs a PX4 ULog or ArduPilot
// DataFlash onboard log through the importer and writes what it produced on
// stdout, one JSON object per line, states, then GNSS fixes, then events:
//
//   {"kind":"state","boot_us":1000000,"record":{...}}
//   {"kind":"gnss","boot_us":999000,"record":{...}}
//   {"kind":"event","boot_us":4132000,"event":{...}}
//
// "record" and "event" are the ics.v1.PliRecord and PliEvent in protobuf's
// JSON form, with proto field names and zeros written out; boot_us is the
// autopilot's boot time the log gave them, for the drift fit (ICS-026).
//
// Usage: ics-log-import LOG LATITUDE LONGITUDE HEIGHT [SYSTEM=ROLE]...
//
// LATITUDE, LONGITUDE and HEIGHT (metres above the ellipsoid) are the range
// ENU frame's origin. Each SYSTEM=ROLE gives a MAVLink system's role: target,
// interceptor, debris or other. The geoid grid is read from where the ICS
// images install it. Counts go to stderr. Exits 1 when the log cannot be
// read or imported, and 2 for bad arguments.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <google/protobuf/util/json_util.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/flightlog/import.hpp"
#include "ics/flightlog/ulog.hpp"
#include "ics/frames/egm96.hpp"
#include "ics/frames/enu.hpp"
#include "ics/frames/geodetic.hpp"

namespace {

using ics::flightlog::ImportSettings;
using ics::flightlog::LogContents;

constexpr std::size_t kFirstRole = 5;
constexpr std::uint64_t kMaxSystem = 255;

struct Plan {
  std::string path;
  ics::frames::Geodetic origin;
  ImportSettings settings;
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

[[nodiscard]] std::optional<ics::v1::EntityRole> role_named(const std::string_view name) noexcept {
  if (name == "target") {
    return ics::v1::ENTITY_ROLE_TARGET;
  }
  if (name == "interceptor") {
    return ics::v1::ENTITY_ROLE_INTERCEPTOR;
  }
  if (name == "debris") {
    return ics::v1::ENTITY_ROLE_DEBRIS;
  }
  if (name == "other") {
    return ics::v1::ENTITY_ROLE_OTHER;
  }
  return std::nullopt;
}

// SYSTEM=ROLE, or nothing.
[[nodiscard]] std::optional<ics::flightlog::RoleAssignment> assignment(const std::string_view text) {
  const std::size_t equals = text.find('=');
  const std::optional<std::uint64_t> system =
      equals == std::string_view::npos ? std::nullopt : number<std::uint64_t>(text.substr(0, equals));
  const std::optional<ics::v1::EntityRole> role =
      system && *system <= kMaxSystem ? role_named(text.substr(equals + 1)) : std::nullopt;
  if (!role) {
    return std::nullopt;
  }
  return ics::flightlog::RoleAssignment{.system = static_cast<std::uint32_t>(*system), .role = *role};
}

[[nodiscard]] std::optional<Plan> parse(const std::span<const char* const> args) {
  if (args.size() < kFirstRole) {
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
  Plan plan{.path = args[1], .origin = *origin, .settings = {}};
  for (const char* const text : args.subspan(kFirstRole)) {
    const std::optional<ics::flightlog::RoleAssignment> assigned = assignment(text);
    if (!assigned) {
      return std::nullopt;
    }
    plan.settings.roles.push_back(*assigned);
  }
  return plan;
}

// A whole log file, if it is a regular file no larger than the readers take.
[[nodiscard]] std::optional<std::string> read_log(const std::string& path) {
  std::error_code error;
  const bool regular = std::filesystem::is_regular_file(path, error);
  const std::uintmax_t size = regular ? std::filesystem::file_size(path, error) : 0;
  std::ifstream file(path, std::ios::binary);
  if (!regular || error || size > ics::flightlog::ULog::kMaxBytes || !file.is_open()) {
    return std::nullopt;
  }
  std::string contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  return contents;
}

// The JSON of a message, or nothing if protobuf cannot write it.
[[nodiscard]] std::optional<std::string> json(const google::protobuf::Message& message) {
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  options.always_print_fields_with_no_presence = true;
  std::string out;
  if (!google::protobuf::util::MessageToJsonString(message, &out, options).ok()) {
    return std::nullopt;
  }
  return out;
}

// Prints one line: true unless protobuf could not write the message.
bool print(const char* const kind, const std::int64_t boot_us, const char* const field,
           const google::protobuf::Message& message) {
  const std::optional<std::string> text = json(message);
  if (!text) {
    return false;
  }
  std::printf("{\"kind\":\"%s\",\"boot_us\":%lld,\"%s\":%s}\n", kind, static_cast<long long>(boot_us), field,
              text->c_str());
  return true;
}

bool print_all(const LogContents& contents) {
  bool printed = true;
  for (const ics::flightlog::LogRecord& state : contents.states) {
    printed = print("state", state.boot_us, "record", state.record) && printed;
  }
  for (const ics::flightlog::LogRecord& fix : contents.gnss) {
    printed = print("gnss", fix.boot_us, "record", fix.record) && printed;
  }
  for (const ics::flightlog::LogEvent& event : contents.events) {
    printed = print("event", event.boot_us, "event", event.event) && printed;
  }
  return printed;
}

void report(const LogContents& contents) {
  std::fprintf(stderr,
               "system %u, %s, states %zu, GNSS fixes %zu, events %zu, left out %zu, GNSS times %zu "
               "(%zu past the leap-second table)\n",
               contents.system_id, contents.timed ? "timed by GNSS" : "untimed", contents.states.size(),
               contents.gnss.size(), contents.events.size(), contents.counts.unplaced, contents.counts.gnss_times,
               contents.counts.beyond_leap_table);
}

int import(const Plan& plan) {
  const ics::Result<ics::frames::Egm96> geoid = ics::frames::Egm96::load(ics::frames::Egm96::kDefaultPath);
  const std::optional<std::string> log = read_log(plan.path);
  if (!geoid || !log) {
    std::fprintf(stderr, "%s\n", geoid ? ("cannot read " + plan.path).c_str() : "cannot read the EGM96 grid");
    return 1;
  }
  const ics::Result<LogContents> contents = ics::flightlog::import_log(
      std::as_bytes(std::span<const char>(*log)), plan.settings, *geoid, ics::frames::EnuFrame(plan.origin));
  if (!contents) {
    std::fprintf(stderr, "%s is not a log ICS can read\n", plan.path.c_str());
    return 1;
  }
  const bool printed = print_all(*contents);
  report(*contents);
  return printed ? 0 : 1;
}

}  // namespace

int main(const int argc, char* argv[]) {
  const std::optional<Plan> plan = parse(std::span<const char* const>(argv, static_cast<std::size_t>(argc)));
  if (!plan) {
    std::fputs("usage: ics-log-import LOG LATITUDE LONGITUDE HEIGHT [SYSTEM=ROLE]...\n", stderr);
    return 2;
  }
  return import(*plan);
}
