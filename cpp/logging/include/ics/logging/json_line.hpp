#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include "ics/common/units.hpp"

namespace ics::logging {

// How serious an event is. A logger writes the events at or above its
// threshold.
enum class Level : std::uint8_t {
  kDebug = 0,
  kInfo = 1,
  kWarn = 2,
  kError = 3,
};

// The name a log line gives the level: "debug", "info", "warn" or "error".
[[nodiscard]] std::string_view to_string(Level level) noexcept;

// The value of a field: an integer, a floating-point number, a boolean or
// text. An unsigned 64-bit count does not convert implicitly, because not every
// value fits; cast it to std::int64_t where it is known to fit.
using Value = std::variant<std::int64_t, double, bool, std::string_view>;

// A named value attached to an event, such as {"frames", 12}. A field refers to
// its key and text rather than copying them, so build fields in the logging
// call itself. Keys are lower_snake_case and must not be one of the names every
// line already has: ts, level, service and event.
struct Field {
  std::string_view key;
  Value value;
};

// One JSON log line (ICS-016), without the newline:
//
//   {"ts":"2026-10-01T09:53:50.123456789Z","level":"info","service":"ics-timingd",
//    "event":"clock_locked","offset_ns":-42}
//
// Text is escaped as JSON requires and assumed to be UTF-8. A floating-point
// value that is not finite is written as the string "NaN", "Infinity" or
// "-Infinity", which JSON numbers cannot express. A field whose key breaks the
// rules above fails an ics::check and is left out.
[[nodiscard]] std::string format_line(UtcTime time, Level level, std::string_view service, std::string_view event,
                                      std::span<const Field> fields);

// A time in RFC 3339 form with nanoseconds, such as "2026-10-01T09:53:50.123456789Z".
[[nodiscard]] std::string format_utc(UtcTime time);

}  // namespace ics::logging
