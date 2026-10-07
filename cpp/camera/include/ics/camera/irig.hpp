#pragma once

#include <cstdint>
#include <optional>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {

// An IRIG-B time stamp as a camera's decoder latches it for a frame: the
// time of year in UTC, to the microsecond, and the year when the IRIG source
// sends it in its IEEE 1344 control functions.
struct IrigStamp {
  // 1 to 366.
  std::uint16_t day_of_year = 0;
  std::uint8_t hours = 0;
  std::uint8_t minutes = 0;
  // 0 to 59: a leap second (60) has no UTC time ICS can count
  // (docs/frames-and-time.md#time).
  std::uint8_t seconds = 0;
  std::uint32_t microseconds = 0;
  // The full year, 1970 to 2261.
  std::optional<std::uint16_t> year;
};

// UTC from a stamp. A stamp without its year takes the year, of the one
// before reference's, its own and the one after, that puts it nearest
// reference: a stamp from 31 December read on 1 January is last year's.
// Fails with Error::kInvalidArgument for a field out of range, day 366 of a
// year that is not a leap year, or a year before 1970 or after 2261.
[[nodiscard]] Result<UtcTime> utc_from_irig(const IrigStamp& stamp, UtcTime reference);

// The stamp a decoder latches at a time from 1970 to 2261: truncated to the
// microsecond, with its year or without. For the emulated cameras.
[[nodiscard]] IrigStamp irig_from_utc(UtcTime time, bool with_year);

}  // namespace ics::camera
