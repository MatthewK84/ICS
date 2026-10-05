#include "ics/flightlog/gps_time.hpp"

#include <cstdint>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::flightlog {
namespace {

constexpr std::int64_t kNsPerMs = 1'000'000;

std::int64_t utc_ms(const std::uint32_t week, const std::uint32_t ms) {
  const Result<GpsUtc> converted = utc_from_gps(week, ms);
  EXPECT_TRUE(converted.has_value()) << week << " " << ms;
  return converted ? to_utc_ns(converted->utc) / kNsPerMs : 0;
}

TEST(GpsTime, StartsAtTheGpsEpochWithNoOffset) { EXPECT_EQ(utc_ms(0, 0), 315'964'800'000); }

TEST(GpsTime, TakesEighteenSecondsSince2017) {
  // The SITL rig's ArduCopter log: week 2439, 83,387.8 s into it, is
  // 2026-10-04T23:09:29.8Z.
  EXPECT_EQ(utc_ms(2439, 83'387'800), 1'791'155'369'800);
}

TEST(GpsTime, AppliesEachLeapSecondWhenItTakesEffect) {
  // 2017-01-01T00:00:00Z is GPS week 1930, 18 s in. The second before it on
  // the GPS scale is the leap second, 2016-12-31T23:59:60, which POSIX time
  // counts as the second after it.
  EXPECT_EQ(utc_ms(1930, 16'000), 1'483'228'799'000);
  EXPECT_EQ(utc_ms(1930, 17'000), 1'483'228'800'000);
  EXPECT_EQ(utc_ms(1930, 18'000), 1'483'228'800'000);
  EXPECT_EQ(utc_ms(1930, 19'000), 1'483'228'801'000);
  // 1981-07-01T00:00:00Z, after the first leap second: GPS week 77,
  // 259,201 s in.
  EXPECT_EQ(utc_ms(77, 259'199'000), 362'793'599'000);
  EXPECT_EQ(utc_ms(77, 259'200'000), 362'793'600'000);
  EXPECT_EQ(utc_ms(77, 259'201'000), 362'793'600'000);
  EXPECT_EQ(utc_ms(77, 259'202'000), 362'793'601'000);
}

TEST(GpsTime, SaysWhenATimeIsPastTheLeapSecondTable) {
  // 2027-06-28T00:00:00Z is GPS week 2477, 86,418 s in.
  const Result<GpsUtc> last_known = utc_from_gps(2477, 86'417'000);
  const Result<GpsUtc> beyond = utc_from_gps(2477, 86'418'000);
  ASSERT_TRUE(last_known.has_value());
  ASSERT_TRUE(beyond.has_value());
  EXPECT_FALSE(last_known->beyond_table);
  EXPECT_TRUE(beyond->beyond_table);
  EXPECT_EQ(to_utc_ns(beyond->utc) / kNsPerMs, kLeapSecondsKnownUntilUnix * 1'000);
}

TEST(GpsTime, RefusesTimesItCannotHold) {
  EXPECT_EQ(utc_from_gps(2439, 604'800'000).error(), Error::kInvalidArgument);
  EXPECT_EQ(utc_from_gps(10'000, 0).error(), Error::kInvalidArgument);
  EXPECT_TRUE(utc_from_gps(9'999, 604'799'999).has_value());
}

}  // namespace
}  // namespace ics::flightlog
