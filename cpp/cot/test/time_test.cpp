#include "ics/cot/time.hpp"

#include <string_view>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"
#include "ics/common/units.hpp"

namespace ics::cot {
namespace {

// 2026-10-04T12:00:00Z.
constexpr std::int64_t kNoon = 1'791'115'200'000'000'000;

std::int64_t parsed(const std::string_view text) {
  const Result<UtcTime> time = parse_cot_time(text);
  EXPECT_TRUE(time.has_value()) << text;
  return to_utc_ns(time.value_or(UtcTime{}));
}

TEST(ParseCotTime, ReadsUtcWithAndWithoutAFraction) {
  EXPECT_EQ(parsed("2026-10-04T12:00:00Z"), kNoon);
  EXPECT_EQ(parsed("2026-10-04T12:00:00.5Z"), kNoon + 500'000'000);
  EXPECT_EQ(parsed("2026-10-04T12:00:00.250Z"), kNoon + 250'000'000);
  EXPECT_EQ(parsed("2026-10-04T12:00:00.000000001Z"), kNoon + 1);
  EXPECT_EQ(parsed("1970-01-01T00:00:00Z"), 0);
  EXPECT_EQ(parsed("2200-12-31T23:59:59.999999999Z"), 7'289'654'399'999'999'999);
}

TEST(ParseCotTime, ConvertsAnOffsetToUtc) {
  EXPECT_EQ(parsed("2026-10-04T13:00:00+01:00"), kNoon);
  EXPECT_EQ(parsed("2026-10-04T05:30:00.000250-06:30"), kNoon + 250'000);
  EXPECT_EQ(parsed("2026-10-04T12:00:00+00:00"), kNoon);
}

TEST(ParseCotTime, RejectsWhatIsNotADateTime) {
  for (const std::string_view text : {
           "",
           "2026-10-04T12:00:00",            // no zone
           "2026-10-04 12:00:00Z",           // a space for the T
           "2026/10/04T12:00:00Z",           // slashes
           "2026-10-04T12-00-00Z",           // dashes in the time
           "20x6-10-04T12:00:00Z",           // year
           "2026-1x-04T12:00:00Z",           // month
           "2026-10-0xT12:00:00Z",           // day
           "2026-10-04Tx2:00:00Z",           // hour
           "2026-10-04T12:x0:00Z",           // minute
           "2026-10-04T12:00:0xZ",           // second
           "2026-10-04T12:00:00.Z",          // a dot without digits
           "2026-10-04T12:00:00.0000000001Z",  // 10 fraction digits
           "2026-10-04T12:00:00z",           // a lowercase zone
           "2026-10-04T12:00:00ZZ",          // trailing text
           "2026-10-04T12:00:00+1:00",       // a short offset
           "2026-10-04T12:00:00*01:00",      // no sign
           "2026-10-04T12:00:00+01-00",      // no colon
           "2026-10-04T12:00:00+0x:00",      // offset hours
           "2026-10-04T12:00:00+01:0x",      // offset minutes
       }) {
    EXPECT_EQ(parse_cot_time(text).error(), Error::kMalformed) << text;
  }
}

TEST(ParseCotTime, RejectsImpossibleDatesAndTimes) {
  for (const std::string_view text : {
           "1969-12-31T23:59:59Z",  // before 1970
           "2201-01-01T00:00:00Z",  // after 2200
           "2026-02-29T00:00:00Z",  // not a leap year
           "2026-13-01T00:00:00Z",
           "2026-10-04T24:00:00Z",
           "2026-10-04T12:60:00Z",
           "2026-10-04T23:59:60Z",  // a leap second
           "2026-10-04T12:00:00+24:00",
           "2026-10-04T12:00:00+01:60",
       }) {
    EXPECT_EQ(parse_cot_time(text).error(), Error::kMalformed) << text;
  }
  EXPECT_TRUE(parse_cot_time("2028-02-29T00:00:00Z").has_value());
}

}  // namespace
}  // namespace ics::cot
