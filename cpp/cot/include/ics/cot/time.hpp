#pragma once

#include <string_view>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::cot {

// A CoT time (ICS-022): an XML Schema dateTime as CoT writes its time, start
// and stale attributes, such as "2026-10-04T12:00:00.250Z". It takes
// YYYY-MM-DDThh:mm:ss, an optional fraction of 1 to 9 digits, then "Z" or an
// offset "+hh:mm" or "-hh:mm", and nothing else. Fails with Error::kMalformed
// for any other text, an impossible date or time of day, a leap second, which
// UTC in ICS cannot hold (docs/frames-and-time.md), or a year outside 1970 to
// 2200.
[[nodiscard]] Result<UtcTime> parse_cot_time(std::string_view text) noexcept;

}  // namespace ics::cot
