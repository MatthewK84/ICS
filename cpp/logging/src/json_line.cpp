#include "ics/logging/json_line.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include "ics/common/check.hpp"
#include "ics/common/units.hpp"

namespace ics::logging {
namespace {

constexpr std::array<std::string_view, 4> kReservedKeys{"ts", "level", "service", "event"};
constexpr std::string_view kKeyStart = "abcdefghijklmnopqrstuvwxyz";
constexpr std::string_view kKeyCharacters = "abcdefghijklmnopqrstuvwxyz0123456789_";
constexpr std::string_view kHexDigits = "0123456789abcdef";
constexpr unsigned char kFirstPrintable = 0x20U;
constexpr std::size_t kTypicalLineBytes = 256;
constexpr std::size_t kUtcTextBytes = 30;
constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;
constexpr std::int64_t kSecondsPerDay = 86'400;
// Holds the shortest form of any double, which is at most 24 characters.
constexpr std::size_t kDoubleTextBytes = 32;

bool is_valid_key(const std::string_view key) {
  return !key.empty() && kKeyStart.find(key.front()) != std::string_view::npos &&
         key.find_first_not_of(kKeyCharacters) == std::string_view::npos &&
         std::ranges::find(kReservedKeys, key) == kReservedKeys.end();
}

// A quotient rounded toward negative infinity, and the remainder from 0 up to
// the divisor, which must be positive. Neither overflows.
struct Split {
  std::int64_t quotient;
  std::int64_t remainder;
};

Split floor_divide(const std::int64_t value, const std::int64_t divisor) {
  Split split{value / divisor, value % divisor};
  if (split.remainder < 0) {
    --split.quotient;
    split.remainder += divisor;
  }
  return split;
}

// Appends a non-negative value in decimal, zero-padded to width digits.
void append_padded(std::string& out, const std::int64_t value, const std::size_t width) {
  const std::string digits = std::to_string(value);
  if (digits.size() < width) {
    out.append(width - digits.size(), '0');
  }
  out.append(digits);
}

// Appends one character of a JSON string, escaped where RFC 8259 requires.
void append_escaped(std::string& out, const char character) {
  switch (character) {
    case '"':
      out.append("\\\"");
      return;
    case '\\':
      out.append("\\\\");
      return;
    case '\n':
      out.append("\\n");
      return;
    case '\r':
      out.append("\\r");
      return;
    case '\t':
      out.append("\\t");
      return;
    default:
      break;
  }
  const auto byte = static_cast<unsigned char>(character);
  if (byte < kFirstPrintable) {
    out.append("\\u00");
    out.push_back(kHexDigits[byte / 16U]);
    out.push_back(kHexDigits[byte % 16U]);
    return;
  }
  out.push_back(character);
}

void append_string(std::string& out, const std::string_view text) {
  out.push_back('"');
  for (const char character : text) {
    append_escaped(out, character);
  }
  out.push_back('"');
}

void append_number(std::string& out, const double value) {
  if (std::isnan(value)) {
    out.append("\"NaN\"");
    return;
  }
  if (std::isinf(value)) {
    out.append(value > 0.0 ? "\"Infinity\"" : "\"-Infinity\"");
    return;
  }
  std::array<char, kDoubleTextBytes> text{};
  const std::to_chars_result written = std::to_chars(text.begin(), text.end(), value);
  out.append(std::string_view(text.begin(), written.ptr));
}

// Appends a field's value as JSON.
class ValueWriter {
 public:
  explicit ValueWriter(std::string& out) noexcept : out_(&out) {}

  void operator()(const std::int64_t value) const { out_->append(std::to_string(value)); }
  void operator()(const double value) const { append_number(*out_, value); }
  void operator()(const bool value) const { out_->append(value ? "true" : "false"); }
  void operator()(const std::string_view value) const { append_string(*out_, value); }

 private:
  std::string* out_;
};

void append_field(std::string& line, const Field& field) {
  if (!check(is_valid_key(field.key))) {
    return;
  }
  line.push_back(',');
  append_string(line, field.key);
  line.push_back(':');
  std::visit(ValueWriter(line), field.value);
}

}  // namespace

std::string_view to_string(const Level level) noexcept {
  switch (level) {
    case Level::kDebug:
      return "debug";
    case Level::kInfo:
      return "info";
    case Level::kWarn:
      return "warn";
    case Level::kError:
      return "error";
  }
  return "unknown";
}

std::string format_line(const UtcTime time, const Level level, const std::string_view service,
                        const std::string_view event, const std::span<const Field> fields) {
  std::string line;
  line.reserve(kTypicalLineBytes);
  line.append("{\"ts\":\"");
  line.append(format_utc(time));
  line.append("\",\"level\":");
  append_string(line, to_string(level));
  line.append(",\"service\":");
  append_string(line, service);
  line.append(",\"event\":");
  append_string(line, event);
  for (const Field& field : fields) {
    append_field(line, field);
  }
  line.push_back('}');
  return line;
}

std::string format_utc(const UtcTime time) {
  // Split on the raw count: converting a floored time back to nanoseconds
  // would overflow within a day of the earliest UtcTime.
  const Split seconds = floor_divide(to_utc_ns(time), kNanosecondsPerSecond);
  const Split days = floor_divide(seconds.quotient, kSecondsPerDay);
  const std::chrono::year_month_day date{
      std::chrono::sys_days(std::chrono::days(static_cast<std::chrono::days::rep>(days.quotient)))};
  const std::chrono::hh_mm_ss<std::chrono::seconds> clock{std::chrono::seconds(days.remainder)};
  std::string text;
  text.reserve(kUtcTextBytes);
  append_padded(text, static_cast<int>(date.year()), 4);
  text.push_back('-');
  append_padded(text, static_cast<unsigned>(date.month()), 2);
  text.push_back('-');
  append_padded(text, static_cast<unsigned>(date.day()), 2);
  text.push_back('T');
  append_padded(text, clock.hours().count(), 2);
  text.push_back(':');
  append_padded(text, clock.minutes().count(), 2);
  text.push_back(':');
  append_padded(text, clock.seconds().count(), 2);
  text.push_back('.');
  append_padded(text, seconds.remainder, 9);
  text.push_back('Z');
  return text;
}

}  // namespace ics::logging
