#include "ics/capd/config.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>

#include <net/if.h>

namespace ics::capd {
namespace {

constexpr std::int64_t kMinSnaplen = 64;
// libpcap's largest, MAXIMUM_SNAPLEN.
constexpr std::int64_t kMaxSnaplen = 262'144;
constexpr std::int64_t kMiB = std::int64_t{1} << 20U;
constexpr std::int64_t kMinBuffer = kMiB;
constexpr std::int64_t kMaxBuffer = std::int64_t{1} << 30U;
constexpr Duration kMinRotateInterval = std::chrono::seconds(1);
constexpr Duration kMaxRotateInterval = std::chrono::hours(24);
constexpr std::int64_t kMinRotateBytes = kMiB;
constexpr std::int64_t kMaxRotateBytes = std::int64_t{1} << 40U;
// PTP domains a default-profile clock may use, as ics-timingd allows.
constexpr std::int64_t kMaxDomain = 127;

constexpr std::array<config::Choice<capture::TimestampSource>, 2> kTimestamps{{
    {"adapter", capture::TimestampSource::kAdapter},
    {"host", capture::TimestampSource::kHost},
}};

[[nodiscard]] PtpConfig read_ptp(config::Reader& root) {
  config::Reader ptp = root.section("ptp");
  PtpConfig out;
  out.ptp4l_socket = ptp.text("ptp4l_socket");
  out.client_socket = ptp.text("client_socket");
  out.ptp_domain = static_cast<std::uint8_t>(ptp.integer("ptp_domain", 0, kMaxDomain));
  return out;
}

}  // namespace

Config read_config(config::Reader& root) {
  Config out;
  out.log = config::read_logging(root);
  config::Reader capture = root.section("capture");
  out.interfaces = capture.texts("interfaces", 1, kMaxInterfaces);
  out.folder = capture.text("folder");
  out.snaplen = static_cast<std::uint32_t>(capture.integer("snaplen", kMinSnaplen, kMaxSnaplen));
  out.buffer_bytes = static_cast<std::uint32_t>(capture.integer("buffer_bytes", kMinBuffer, kMaxBuffer));
  out.timestamps = capture.choice("timestamps", kTimestamps);
  out.rotation.max_age = capture.duration("rotate_interval_ns", kMinRotateInterval, kMaxRotateInterval);
  out.rotation.max_bytes = static_cast<std::uint64_t>(capture.integer("rotate_bytes", kMinRotateBytes, kMaxRotateBytes));
  if (out.timestamps == capture::TimestampSource::kAdapter) {
    out.ptp = read_ptp(root);
  }
  return out;
}

bool valid_interface_name(const std::string_view name) noexcept {
  const bool special = name.empty() || name.size() >= IFNAMSIZ || name == "." || name == "..";
  const bool bad_character = std::ranges::any_of(name, [](const char c) {
    return c == '/' || c == ':' || std::isspace(static_cast<unsigned char>(c)) != 0;
  });
  return !special && !bad_character;
}

}  // namespace ics::capd
