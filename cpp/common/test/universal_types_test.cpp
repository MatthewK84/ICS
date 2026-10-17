// universal_types_test.cpp: the universal types have the sizes, signedness
// and floating-point formats their names give (JSF AV Rules 146 and 209).
#include <cstddef>
#include <limits>

#include <gtest/gtest.h>

#include <ics/common/universal_types.h>

namespace
{

// The bytes in each 8, 16, 32 and 64-bit type.
constexpr std::size_t bytes_8 = 1;
constexpr std::size_t bytes_16 = 2;
constexpr std::size_t bytes_32 = 4;
constexpr std::size_t bytes_64 = 8;

// JSF deviation D-5: GoogleTest's TEST and EXPECT macros fix the form of the
// test names and expand to code of GoogleTest's own (AV 50, 51 and 209).
TEST(UniversalTypes, HaveTheSizesTheirNamesGive)
{
  EXPECT_EQ(sizeof(ics::int8), bytes_8);
  EXPECT_EQ(sizeof(ics::int16), bytes_16);
  EXPECT_EQ(sizeof(ics::int32), bytes_32);
  EXPECT_EQ(sizeof(ics::int64), bytes_64);
  EXPECT_EQ(sizeof(ics::uint8), bytes_8);
  EXPECT_EQ(sizeof(ics::uint16), bytes_16);
  EXPECT_EQ(sizeof(ics::uint32), bytes_32);
  EXPECT_EQ(sizeof(ics::uint64), bytes_64);
  EXPECT_EQ(sizeof(ics::float32), bytes_32);
  EXPECT_EQ(sizeof(ics::float64), bytes_64);
}

TEST(UniversalTypes, AreSignedOrUnsignedAsNamed)
{
  EXPECT_TRUE(std::numeric_limits<ics::int8>::is_signed);
  EXPECT_TRUE(std::numeric_limits<ics::int64>::is_signed);
  EXPECT_FALSE(std::numeric_limits<ics::uint8>::is_signed);
  EXPECT_FALSE(std::numeric_limits<ics::uint64>::is_signed);
}

TEST(UniversalTypes, AreIeee754)
{
  EXPECT_TRUE(std::numeric_limits<ics::float32>::is_iec559);
  EXPECT_TRUE(std::numeric_limits<ics::float64>::is_iec559);
}

}  // namespace
