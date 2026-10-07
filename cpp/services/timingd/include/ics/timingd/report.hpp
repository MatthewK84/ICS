#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "ics/common/units.hpp"
#include "ics/timing/clock_state.hpp"
#include "ics/timing/quality.hpp"
#include "ics/v1/time_quality.pb.h"

namespace ics::timingd {

// The ics.v1.TimeQuality reports ics-timingd publishes (ICS-019).

// The proto value of a clock state.
[[nodiscard]] v1::TimeQuality::ClockState to_proto(timing::ClockState state) noexcept;

// Sets the fields of report that change with each poll from quality, sampled
// at utc. An unbounded error is INT64_MAX nanoseconds. station_id is set once,
// when the report is made, and camera_offsets whenever the strobe
// calibration's offsets file changes (ICS-029); PTP does not carry
// gnss_satellite_count or irig_b_locked, so they stay 0 and false.
void fill(const timing::Quality& quality, UtcTime utc, v1::TimeQuality& report) noexcept;

// The most bytes a report like this one can take: its station_id and
// camera_offsets, and every other field at its longest encoding.
[[nodiscard]] std::size_t max_report_size(const v1::TimeQuality& report);

// report serialized into buffer: the bytes written, or none when it does not
// fit, which an ics::check reports. No bytes read as an empty TimeQuality,
// whose clock state is CLOCK_STATE_UNSPECIFIED.
[[nodiscard]] std::span<const std::byte> serialize(const v1::TimeQuality& report, std::span<std::byte> buffer) noexcept;

}  // namespace ics::timingd
