#include "ics/tagger/counter.hpp"

#include <cstdint>

#include <gtest/gtest.h>

namespace {

using ics::tagger::CounterExtender;

constexpr std::uint64_t kWrap = std::uint64_t{1} << 32U;

TEST(TaggerCounter, CountsFromTheFirstReading) {
  CounterExtender counter;
  EXPECT_EQ(counter.extend(0x10U), kWrap + 0x10U);
  EXPECT_EQ(counter.extend(0x10U), kWrap + 0x10U);
  EXPECT_EQ(counter.extend(0x7FFF'0000U), kWrap + 0x7FFF'0000U);
}

TEST(TaggerCounter, CountsEachWrap) {
  CounterExtender counter;
  EXPECT_EQ(counter.extend(0xFFFF'FFF0U), kWrap + 0xFFFF'FFF0U);
  EXPECT_EQ(counter.extend(0x20U), (2 * kWrap) + 0x20U);
  EXPECT_EQ(counter.extend(0x7FFF'0000U), (2 * kWrap) + 0x7FFF'0000U);
  EXPECT_EQ(counter.extend(0xFFFE'0000U), (2 * kWrap) + 0xFFFE'0000U);
  EXPECT_EQ(counter.extend(0x0U), 3 * kWrap);
}

TEST(TaggerCounter, CountsAReadingALittleEarlierThanTheLast) {
  CounterExtender counter;
  EXPECT_EQ(counter.extend(0x5U), kWrap + 0x5U);
  // Before the first reading, and across a wrap backwards.
  EXPECT_EQ(counter.extend(0xFFFF'FFFBU), kWrap - 0x5U);
  EXPECT_EQ(counter.extend(0x10U), kWrap + 0x10U);
  EXPECT_EQ(counter.extend(0x8U), kWrap + 0x8U);
}

}  // namespace
