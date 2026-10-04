#include "ics/cot/time.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <string_view>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::cot {
namespace {

using std::chrono::hours;
using std::chrono::minutes;
using std::chrono::seconds;

// YYYY-MM-DDThh:mm:ss
constexpr std::size_t kDateTimeSize = 19;
constexpr std::size_t kYearSize = 4;
constexpr std::size_t kFieldSize = 2;
constexpr int kFirstYear = 1970;
constexpr int kLastYear = 2200;
constexpr int kHoursPerDay = 24;
constexpr int kMinutesPerHour = 60;
constexpr int kSecondsPerMinute = 60;
constexpr int kDecimal = 10;
// Nanoseconds in one unit of a fraction of so many digits, up to nine.
constexpr auto kFractionScale = std::to_array<std::int64_t>(
    {0, 100'000'000, 10'000'000, 1'000'000, 100'000, 10'000, 1'000, 100, 10, 1});
// +hh:mm or -hh:mm
constexpr std::size_t kOffsetSize = 6;

struct Separator {
  std::size_t at = 0;
  char separator = '\0';
};

constexpr std::array<Separator, 5> kSeparators{{{4, '-'}, {7, '-'}, {10, 'T'}, {13, ':'}, {16, ':'}}};

[[nodiscard]] bool is_digit(const char c) noexcept { return c >= '0' && c <= '9'; }

// The decimal number in text[at, at + size), or nothing unless all of it is digits.
[[nodiscard]] std::optional<int> number(const std::string_view text, const std::size_t at,
                                        const std::size_t size) noexcept {
  const std::string_view digits = text.substr(std::min(at, text.size()), size);
  if (digits.size() != size || !std::ranges::all_of(digits, is_digit)) {
    return std::nullopt;
  }
  return std::accumulate(digits.begin(), digits.end(), 0,
                         [](const int value, const char c) { return (value * kDecimal) + (c - '0'); });
}

// The UTC time the first 19 characters name, before any fraction or offset.
[[nodiscard]] std::optional<UtcTime> date_time(const std::string_view text) noexcept {
  const bool separated = std::ranges::all_of(
      kSeparators, [text](const Separator& expected) { return text[expected.at] == expected.separator; });
  const std::optional<int> year = number(text, 0, kYearSize);
  const std::optional<int> month = number(text, 5, kFieldSize);
  const std::optional<int> day = number(text, 8, kFieldSize);
  const std::optional<int> hour = number(text, 11, kFieldSize);
  const std::optional<int> minute = number(text, 14, kFieldSize);
  const std::optional<int> second = number(text, 17, kFieldSize);
  if (!separated || !year || !month || !day || !hour || !minute || !second) {
    return std::nullopt;
  }
  const std::chrono::year_month_day date{std::chrono::year{*year}, std::chrono::month{static_cast<unsigned>(*month)},
                                         std::chrono::day{static_cast<unsigned>(*day)}};
  const bool in_range = *year >= kFirstYear && *year <= kLastYear && date.ok();
  if (!in_range || *hour >= kHoursPerDay || *minute >= kMinutesPerHour || *second >= kSecondsPerMinute) {
    return std::nullopt;
  }
  return UtcTime(std::chrono::sys_days{date}) + hours(*hour) + minutes(*minute) + seconds(*second);
}

// A fraction of a second at the start of rest, which is never empty: its
// length, with the dot, and its value. A rest that does not start with a dot
// holds no fraction.
struct Fraction {
  std::size_t size = 0;
  Duration value{};
};

[[nodiscard]] std::optional<Fraction> fraction(const std::string_view rest) noexcept {
  if (rest.front() != '.') {
    return Fraction{};
  }
  const std::string_view after = rest.substr(1);
  const std::size_t digits = static_cast<std::size_t>(std::ranges::find_if_not(after, is_digit) - after.begin());
  if (digits == 0 || digits >= kFractionScale.size()) {
    return std::nullopt;
  }
  const std::int64_t value = *number(after, 0, digits);
  return Fraction{.size = digits + 1, .value = Duration(value * kFractionScale[digits])};
}

// The offset from UTC that zone names: "Z", "+hh:mm" or "-hh:mm".
[[nodiscard]] std::optional<minutes> zone(const std::string_view zone_text) noexcept {
  if (zone_text == "Z") {
    return minutes(0);
  }
  const std::optional<int> hour = number(zone_text, 1, kFieldSize);
  const std::optional<int> minute = number(zone_text, 4, kFieldSize);
  const bool signed_zone = zone_text.size() == kOffsetSize && (zone_text[0] == '+' || zone_text[0] == '-');
  if (!signed_zone || zone_text[3] != ':' || !hour || !minute || *hour >= kHoursPerDay || *minute >= kMinutesPerHour) {
    return std::nullopt;
  }
  const minutes offset = hours(*hour) + minutes(*minute);
  return zone_text[0] == '-' ? -offset : offset;
}

}  // namespace

Result<UtcTime> parse_cot_time(const std::string_view text) noexcept {
  if (text.size() < kDateTimeSize + 1) {
    return fail(Error::kMalformed);
  }
  const std::optional<UtcTime> local = date_time(text);
  const std::string_view rest = text.substr(kDateTimeSize);
  const std::optional<Fraction> part = fraction(rest);
  const std::optional<minutes> offset = part ? zone(rest.substr(part->size)) : std::nullopt;
  if (!local || !offset) {
    return fail(Error::kMalformed);
  }
  static_cast<void>(check(part->value < seconds(1)));
  return *local + part->value - *offset;
}

}  // namespace ics::cot
