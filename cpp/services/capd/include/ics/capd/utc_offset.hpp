#pragma once

#include "ics/capd/config.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::capd {

// How long ics-capd waits for ptp4l's answers at start-up.
inline constexpr Duration kPtpTimeout = std::chrono::seconds(1);

// TAI - UTC as ptp4l reports it (timePropertiesDS.currentUtcOffset), read
// once at start-up (ICS-020): adapter time stamps are TAI, since ptp4l keeps
// each NIC's clock on the PTP timescale. A leap second needs a restart; none
// is scheduled. kUnavailable when ptp4l does not answer within kPtpTimeout or
// does not mark the offset valid; kInvalidArgument for a socket path too long
// for a socket address.
[[nodiscard]] Result<Duration> read_tai_minus_utc(const PtpConfig& config) noexcept;

}  // namespace ics::capd
