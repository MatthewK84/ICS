#pragma once

#include <cstdint>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::flightlog {

// UTC from GPS time (ICS-025), for logs that carry only GPS time, such as
// ArduPilot's DataFlash GPS messages. GPS time counts from
// 1980-01-06T00:00:00Z without leap seconds, so UTC = GPS - (GPS - UTC), an
// offset that grows by one second at each leap second: 18 s since
// 2017-01-01. The offsets come from IERS Bulletin C, through the tz
// database's leap-seconds.list, which says no other leap second will occur
// before kLeapSecondsKnownUntil; update the table (gps_time.cpp) when IERS
// announces one, or when that date nears.

// 2027-06-28T00:00:00Z, when the leap-seconds.list of 2026-10-03 expires.
inline constexpr std::int64_t kLeapSecondsKnownUntilUnix = 1'814'140'800;

struct GpsUtc {
  UtcTime utc;
  // Whether the time is past kLeapSecondsKnownUntilUnix, so a leap second
  // announced since may be missing from the offset.
  bool beyond_table = false;
};

// GPS time as whole weeks since the GPS epoch and milliseconds into the
// week. Fails with Error::kInvalidArgument for milliseconds of a week or
// more, or a week past 9999 (the year 2171).
[[nodiscard]] Result<GpsUtc> utc_from_gps(std::uint32_t week, std::uint32_t ms_of_week) noexcept;

}  // namespace ics::flightlog
