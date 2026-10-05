#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "ics/v1/pli.pb.h"

namespace ics::mavlink {

// Conversions the MAVLink adapter shares with the onboard-log importer
// (ICS-025), whose autopilots use the same values.

// A GPS_FIX_TYPE value as a PliRecord fix type. PX4's sensor_gps fix_type and
// ArduPilot's GPS status take the same values.
[[nodiscard]] v1::PliRecord::FixType fix_type(std::uint8_t gps_fix_type) noexcept;

// Text with every character outside printable ASCII replaced by '?'.
[[nodiscard]] std::string printable(std::string_view text);

}  // namespace ics::mavlink
