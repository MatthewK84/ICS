#include "ics/camera/irig.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::camera {
namespace {

using std::chrono::days;
using std::chrono::hours;
using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::seconds;

// 1 January, 00:00:00 UTC, of some years.
constexpr std::int64_t k1970Ns = 0;
constexpr std::int64_t k2026Ns = 1'767'225'600'000'000'000;
constexpr std::int64_t k2027Ns = 1'798'761'600'000'000'000;
constexpr std::int64_t k2028Ns = 1'830'297'600'000'000'000;

UtcTime at(const std::int64_t new_year_ns, const days day, const Duration time) {
  return utc_from_ns(new_year_ns) + day + time;
}

TEST(Irig, StampsATimeToTheMicrosecondAndReadsItBack) {
  const UtcTime time = at(k2026Ns, days(279), hours(13) + std::chrono::minutes(4) + seconds(5) + microseconds(678'901));
  const IrigStamp stamp = irig_from_utc(time + Duration(999), true);
  EXPECT_EQ(stamp.day_of_year, 280U);
  EXPECT_EQ(stamp.hours, 13U);
  EXPECT_EQ(stamp.minutes, 4U);
  EXPECT_EQ(stamp.seconds, 5U);
  EXPECT_EQ(stamp.microseconds, 678'901U);
  EXPECT_EQ(stamp.year, std::optional<std::uint16_t>(2026));
  EXPECT_EQ(utc_from_irig(stamp, utc_from_ns(k1970Ns)).value(), time);
  const IrigStamp yearless = irig_from_utc(time, false);
  EXPECT_EQ(yearless.year, std::nullopt);
  EXPECT_EQ(utc_from_irig(yearless, time + days(100)).value(), time);
}

TEST(Irig, TakesAMissingYearNearestTheReference) {
  // The last moment of 2026, read a few seconds into 2027, is 2026's.
  const UtcTime last = at(k2026Ns, days(364), hours(23) + std::chrono::minutes(59) + seconds(59) + milliseconds(999));
  EXPECT_EQ(utc_from_irig(irig_from_utc(last, false), utc_from_ns(k2027Ns) + seconds(5)).value(), last);
  // The first moment of 2027, against a reference late in 2026, is 2027's.
  const UtcTime first = utc_from_ns(k2027Ns) + microseconds(1);
  EXPECT_EQ(utc_from_irig(irig_from_utc(first, false), last).value(), first);
  // 1969 is out of range, so near 1970 only 1970 and 1971 are candidates.
  const IrigStamp late = irig_from_utc(at(k1970Ns, days(364), hours(1)), false);
  EXPECT_EQ(utc_from_irig(late, utc_from_ns(k1970Ns)).value(), at(k1970Ns, days(364), hours(1)));
}

TEST(Irig, KeepsDay366ForLeapYears) {
  const UtcTime leap_day = at(k2028Ns, days(365), hours(12));
  const IrigStamp stamp = irig_from_utc(leap_day, false);
  EXPECT_EQ(stamp.day_of_year, 366U);
  // From 2027 the only leap year in reach is 2028.
  EXPECT_EQ(utc_from_irig(stamp, utc_from_ns(k2027Ns)).value(), leap_day);
  // From 2026 none of 2025, 2026 and 2027 has a day 366.
  EXPECT_EQ(utc_from_irig(stamp, utc_from_ns(k2026Ns)).error(), Error::kInvalidArgument);
  IrigStamp in_2026 = stamp;
  in_2026.year = 2026;
  EXPECT_EQ(utc_from_irig(in_2026, leap_day).error(), Error::kInvalidArgument);
}

TEST(Irig, RefusesFieldsOutOfRange) {
  const IrigStamp good{.day_of_year = 1, .hours = 0, .minutes = 0, .seconds = 0, .microseconds = 0, .year = 2261};
  EXPECT_TRUE(utc_from_irig(good, utc_from_ns(k2026Ns)).has_value());
  const std::vector<std::pair<std::string, std::function<void(IrigStamp&)>>> refused{
      {"day 0", [](IrigStamp& s) { s.day_of_year = 0; }},
      {"day 367", [](IrigStamp& s) { s.day_of_year = 367; }},
      {"hour 24", [](IrigStamp& s) { s.hours = 24; }},
      {"minute 60", [](IrigStamp& s) { s.minutes = 60; }},
      {"leap second", [](IrigStamp& s) { s.seconds = 60; }},
      {"a whole second of microseconds", [](IrigStamp& s) { s.microseconds = 1'000'000; }},
      {"1969", [](IrigStamp& s) { s.year = 1969; }},
      {"2262", [](IrigStamp& s) { s.year = 2262; }},
      {"no year far from 1970 to 2261", [](IrigStamp& s) { s.year = std::nullopt; }},
  };
  for (const auto& [name, change] : refused) {
    IrigStamp stamp = good;
    change(stamp);
    EXPECT_EQ(utc_from_irig(stamp, utc_from_ns(k1970Ns) - days(800)).error(), Error::kInvalidArgument) << name;
  }
}

}  // namespace
}  // namespace ics::camera
