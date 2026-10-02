#include "degrees.hpp"

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

namespace {

using ics::frames::detail::atan2_degrees;
using ics::frames::detail::sincos_degrees;
using ics::frames::detail::SinCos;
using ics::frames::detail::wrap_longitude;

TEST(SincosDegrees, IsExactAtQuarterTurnsInEveryQuadrant) {
  const SinCos zero = sincos_degrees(0.0);
  EXPECT_EQ(zero.sin, 0.0);
  EXPECT_EQ(zero.cos, 1.0);
  const SinCos quarter = sincos_degrees(90.0);
  EXPECT_EQ(quarter.sin, 1.0);
  EXPECT_EQ(quarter.cos, 0.0);
  const SinCos half = sincos_degrees(180.0);
  EXPECT_EQ(half.sin, 0.0);
  EXPECT_EQ(half.cos, -1.0);
  const SinCos three_quarters = sincos_degrees(-90.0);
  EXPECT_EQ(three_quarters.sin, -1.0);
  EXPECT_EQ(three_quarters.cos, 0.0);
  EXPECT_FALSE(std::signbit(three_quarters.cos));
}

TEST(SincosDegrees, MatchesTheLibraryBetweenQuarterTurns) {
  for (const double degrees : {30.0, 135.0, -150.0, -60.0, 89.999, 271.0}) {
    const SinCos result = sincos_degrees(degrees);
    EXPECT_NEAR(result.sin, std::sin(degrees * std::numbers::pi / 180.0), 1e-15) << degrees;
    EXPECT_NEAR(result.cos, std::cos(degrees * std::numbers::pi / 180.0), 1e-15) << degrees;
  }
}

TEST(SincosDegrees, GivesAZeroSineTheAnglesSign) {
  EXPECT_TRUE(std::signbit(sincos_degrees(-0.0).sin));
  EXPECT_FALSE(std::signbit(sincos_degrees(0.0).sin));
  EXPECT_TRUE(std::signbit(sincos_degrees(-180.0).sin));
}

TEST(Atan2Degrees, IsExactAtMultiplesOf45Degrees) {
  EXPECT_EQ(atan2_degrees(0.0, 1.0), 0.0);
  EXPECT_EQ(atan2_degrees(1.0, 1.0), 45.0);
  EXPECT_EQ(atan2_degrees(1.0, 0.0), 90.0);
  EXPECT_EQ(atan2_degrees(1.0, -1.0), 135.0);
  EXPECT_EQ(atan2_degrees(0.0, -1.0), 180.0);
  EXPECT_EQ(atan2_degrees(-0.0, -1.0), -180.0);
  EXPECT_EQ(atan2_degrees(-1.0, -1.0), -135.0);
  EXPECT_EQ(atan2_degrees(-1.0, 0.0), -90.0);
  EXPECT_EQ(atan2_degrees(-1.0, 1.0), -45.0);
}

TEST(Atan2Degrees, MatchesTheLibraryInEveryOctant) {
  for (const double angle : {10.0, 60.0, 120.0, 170.0, -10.0, -60.0, -120.0, -170.0}) {
    const double radians = angle * std::numbers::pi / 180.0;
    EXPECT_NEAR(atan2_degrees(std::sin(radians), std::cos(radians)), angle, 1e-12) << angle;
  }
}

TEST(WrapLongitude, WrapsIntoMinus180To180) {
  EXPECT_EQ(wrap_longitude(0.0), 0.0);
  EXPECT_EQ(wrap_longitude(179.5), 179.5);
  EXPECT_EQ(wrap_longitude(180.0), -180.0);
  EXPECT_EQ(wrap_longitude(-180.0), -180.0);
  EXPECT_EQ(wrap_longitude(540.0), -180.0);
  EXPECT_EQ(wrap_longitude(190.0), -170.0);
  EXPECT_EQ(wrap_longitude(-190.0), 170.0);
}

}  // namespace
