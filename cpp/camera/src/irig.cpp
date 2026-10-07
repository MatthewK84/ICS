#include "ics/camera/irig.hpp"

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <optional>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {
namespace {

using std::chrono::days;
using std::chrono::hours;
using std::chrono::microseconds;
using std::chrono::minutes;
using std::chrono::seconds;
using std::chrono::sys_days;
using std::chrono::year;
using std::chrono::year_month_day;

constexpr int kFirstYear = 1970;
constexpr int kLastYear = 2261;
constexpr std::uint16_t kLeapDay = 366;
constexpr std::uint8_t kHoursPerDay = 24;
constexpr std::uint8_t kSixty = 60;
constexpr std::uint32_t kMicrosecondsPerSecond = 1'000'000;

[[nodiscard]] bool in_range(const IrigStamp& s) noexcept {
  return s.day_of_year >= 1 && s.day_of_year <= kLeapDay && s.hours < kHoursPerDay && s.minutes < kSixty &&
         s.seconds < kSixty && s.microseconds < kMicrosecondsPerSecond;
}

// The stamp's time in a year, if the year holds it.
[[nodiscard]] std::optional<UtcTime> in_year(const IrigStamp& s, const int in) noexcept {
  const year y{in};
  if (in < kFirstYear || in > kLastYear || (s.day_of_year == kLeapDay && !y.is_leap())) {
    return std::nullopt;
  }
  const sys_days new_year{y / std::chrono::January / 1};
  return UtcTime(new_year + days(s.day_of_year - 1)) + hours(s.hours) + minutes(s.minutes) + seconds(s.seconds) +
         microseconds(s.microseconds);
}

// Of the year before reference's, its own and the one after, the time in
// the one nearest reference.
[[nodiscard]] std::optional<UtcTime> nearest(const IrigStamp& s, const UtcTime reference) noexcept {
  const int now = static_cast<int>(year_month_day(std::chrono::floor<days>(reference)).year());
  std::optional<UtcTime> best;
  for (const int candidate : {now - 1, now, now + 1}) {
    const std::optional<UtcTime> t = in_year(s, candidate);
    const bool nearer = t && (!best || std::chrono::abs(*t - reference) < std::chrono::abs(*best - reference));
    best = nearer ? t : best;
  }
  return best;
}

}  // namespace

Result<UtcTime> utc_from_irig(const IrigStamp& stamp, const UtcTime reference) {
  const std::optional<UtcTime> time =
      !in_range(stamp) ? std::nullopt : (stamp.year ? in_year(stamp, *stamp.year) : nearest(stamp, reference));
  if (!time) {
    return fail(Error::kInvalidArgument);
  }
  return *time;
}

IrigStamp irig_from_utc(const UtcTime time, const bool with_year) {
  const sys_days day = std::chrono::floor<days>(time);
  const year_month_day date{day};
  const sys_days new_year{date.year() / std::chrono::January / 1};
  const std::chrono::hh_mm_ss<microseconds> clock{std::chrono::floor<microseconds>(time - day)};
  // A day of any year falls on day 1 to 366 of it.
  static_cast<void>(check((day - new_year).count() < kLeapDay));
  return IrigStamp{.day_of_year = static_cast<std::uint16_t>((day - new_year).count() + 1),
                   .hours = static_cast<std::uint8_t>(clock.hours().count()),
                   .minutes = static_cast<std::uint8_t>(clock.minutes().count()),
                   .seconds = static_cast<std::uint8_t>(clock.seconds().count()),
                   .microseconds = static_cast<std::uint32_t>(clock.subseconds().count()),
                   .year = with_year ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(
                                           static_cast<int>(date.year())))
                                     : std::nullopt};
}

}  // namespace ics::camera
