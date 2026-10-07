#include "ics/timingd/config.hpp"

#include <chrono>
#include <cstdint>

namespace ics::timingd {
namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;

// PTP domains a default-profile clock may use (IEEE 1588-2019 Table 2), as
// ptp4l's domainNumber setting allows.
constexpr std::int64_t kMaxDomain = 127;
constexpr Duration kMinPollInterval = milliseconds(10);
constexpr Duration kMaxPollInterval = seconds(1);
constexpr Duration kMaxAsymmetryBound = seconds(1);
constexpr double kMaxDrift = 1e6;

}  // namespace

Config read_config(config::Reader& root) {
  Config out;
  out.log = config::read_logging(root);
  config::Reader timing = root.section("timing");
  out.station_id = timing.text("station_id");
  out.ptp4l_socket = timing.text("ptp4l_socket");
  out.client_socket = timing.text("client_socket");
  out.publish_socket = timing.text("publish_socket");
  out.camera_offsets_file = timing.text("camera_offsets_file");
  out.ptp_domain = static_cast<std::uint8_t>(timing.integer("ptp_domain", 0, kMaxDomain));
  out.poll_interval = timing.duration("poll_interval_ns", kMinPollInterval, kMaxPollInterval);
  out.model.asymmetry_bound = timing.duration("asymmetry_bound_ns", Duration::zero(), kMaxAsymmetryBound);
  out.model.holdover_drift_ns_per_s = timing.number("holdover_drift_ns_per_s", 0.0, kMaxDrift);
  return out;
}

}  // namespace ics::timingd
