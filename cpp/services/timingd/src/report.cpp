#include "ics/timingd/report.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "ics/common/check.hpp"

namespace ics::timingd {

v1::TimeQuality::ClockState to_proto(const timing::ClockState state) noexcept {
  switch (state) {
    case timing::ClockState::kLocked:
      return v1::TimeQuality::CLOCK_STATE_LOCKED;
    case timing::ClockState::kHoldover:
      return v1::TimeQuality::CLOCK_STATE_HOLDOVER;
    case timing::ClockState::kFreeRunning:
      return v1::TimeQuality::CLOCK_STATE_FREE_RUNNING;
  }
  return v1::TimeQuality::CLOCK_STATE_UNSPECIFIED;
}

void fill(const timing::Quality& quality, const UtcTime utc, v1::TimeQuality& report) noexcept {
  report.set_time_utc_ns(to_utc_ns(utc));
  report.set_clock_state(to_proto(quality.state));
  report.set_holdover_duration_ns(quality.holdover.count());
  report.set_ptp_offset_ns(quality.ptp_offset.count());
  report.set_ptp_path_delay_ns(quality.ptp_path_delay.count());
  report.set_error_bound_ns(quality.error_bound.count());
}

std::size_t max_report_size(const v1::TimeQuality& report) {
  // A negative integer takes the longest varint, 10 bytes.
  constexpr std::int64_t kLongest = -1;
  v1::TimeQuality largest;
  largest.set_station_id(report.station_id());
  *largest.mutable_camera_offsets() = report.camera_offsets();
  largest.set_time_utc_ns(kLongest);
  largest.set_clock_state(v1::TimeQuality::CLOCK_STATE_FREE_RUNNING);
  largest.set_holdover_duration_ns(kLongest);
  largest.set_gnss_satellite_count(std::numeric_limits<std::uint32_t>::max());
  largest.set_ptp_offset_ns(kLongest);
  largest.set_ptp_path_delay_ns(kLongest);
  largest.set_irig_b_locked(true);
  largest.set_error_bound_ns(kLongest);
  return largest.ByteSizeLong();
}

std::span<const std::byte> serialize(const v1::TimeQuality& report, const std::span<std::byte> buffer) noexcept {
  const std::size_t size = std::min(report.ByteSizeLong(), buffer.size());
  const bool written = report.SerializeToArray(buffer.data(), static_cast<int>(size));
  return buffer.first(ics::check(written) ? size : 0);
}

}  // namespace ics::timingd
