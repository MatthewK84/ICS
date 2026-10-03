#include "ics/capd/config.hpp"

#include <array>
#include <chrono>
#include <cstdint>

namespace ics::capd {
namespace {

constexpr std::int64_t kMinSnaplen = 64;
// libpcap's largest snapshot length.
constexpr std::int64_t kMaxSnaplen = 262'144;
constexpr std::int64_t kMiB = std::int64_t{1} << 20U;
constexpr std::int64_t kMaxRingBytes = 1024 * kMiB;
constexpr std::int64_t kMaxRotateBytes = std::int64_t{1} << 40U;
constexpr Duration kMinRotateInterval = std::chrono::seconds(1);
constexpr Duration kMaxRotateInterval = std::chrono::hours(24);
// PTP domains a default-profile clock may use, as ptp4l's domainNumber allows.
constexpr std::int64_t kMaxDomain = 127;
constexpr std::array<config::Choice<capture::TimestampSource>, 2> kTimestamps{{
    {"adapter", capture::TimestampSource::kAdapter},
    {"host", capture::TimestampSource::kHost},
}};

}  // namespace

Config read_config(config::Reader& root) {
  Config out;
  out.log = config::read_logging(root);
  config::Reader capture = root.section("capture");
  out.interfaces = capture.texts("interfaces", kMaxInterfaces);
  out.output_dir = capture.text("output_dir");
  out.snaplen = static_cast<std::uint32_t>(capture.integer("snaplen", kMinSnaplen, kMaxSnaplen));
  out.ring_bytes = static_cast<std::uint32_t>(capture.integer("ring_bytes", kMiB, kMaxRingBytes));
  out.timestamps = capture.choice("timestamps", kTimestamps);
  out.rotation.interval = capture.duration("rotate_interval_ns", kMinRotateInterval, kMaxRotateInterval);
  out.rotation.max_bytes = static_cast<std::uint64_t>(capture.integer("rotate_bytes", kMiB, kMaxRotateBytes));
  out.ptp4l_socket = capture.text("ptp4l_socket");
  out.client_socket = capture.text("client_socket");
  out.ptp_domain = static_cast<std::uint8_t>(capture.integer("ptp_domain", 0, kMaxDomain));
  return out;
}

}  // namespace ics::capd
