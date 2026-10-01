#include "ics/common/units.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

template <typename A, typename B>
concept Addable = requires(A a, B b) { a + b; };

template <typename A, typename B>
concept Multipliable = requires(A a, B b) { a * b; };

template <typename A, typename B>
concept Comparable = requires(A a, B b) { a < b; };

// Mixing units, or a unit and a bare double, does not compile.
static_assert(Addable<ics::Meters, ics::Meters>);
static_assert(!Addable<ics::Meters, ics::Radians>);
static_assert(!Addable<ics::Radians, ics::Degrees>);
static_assert(!Addable<ics::Meters, double>);
static_assert(!Comparable<ics::Meters, ics::Radians>);
static_assert(Multipliable<ics::Meters, double>);
static_assert(!Multipliable<ics::Meters, ics::Meters>);
static_assert(!std::is_convertible_v<double, ics::Meters>);
static_assert(!std::is_convertible_v<ics::Degrees, ics::Radians>);
static_assert(!std::is_convertible_v<ics::Meters, double>);
static_assert(!std::is_convertible_v<std::int64_t, ics::UtcTime>);
// A quantity is a double and nothing more.
static_assert(sizeof(ics::Meters) == sizeof(double));
static_assert(std::is_trivially_copyable_v<ics::Meters>);

TEST(Units, ArithmeticKeepsTheUnit) {
  const ics::Meters a(3.0);
  const ics::Meters b(1.5);
  EXPECT_EQ((a + b).value(), 4.5);
  EXPECT_EQ((a - b).value(), 1.5);
  EXPECT_EQ((-a).value(), -3.0);
  EXPECT_EQ((a * 2.0).value(), 6.0);
  EXPECT_EQ((2.0 * a).value(), 6.0);
  EXPECT_EQ((a / 2.0).value(), 1.5);
  EXPECT_EQ(a / b, 2.0);
  EXPECT_EQ(ics::Meters().value(), 0.0);
}

TEST(Units, ComparesWithinAUnit) {
  EXPECT_LT(ics::Meters(1.0), ics::Meters(2.0));
  EXPECT_LE(ics::Meters(2.0), ics::Meters(2.0));
  EXPECT_FALSE(ics::Meters(2.0) < ics::Meters(2.0));
  EXPECT_EQ(ics::Radians(0.5), ics::Radians(0.5));
  EXPECT_NE(ics::Degrees(1.0), ics::Degrees(2.0));
  // NaN is unordered, as for a double.
  const ics::Meters nan(std::numeric_limits<double>::quiet_NaN());
  EXPECT_FALSE(nan == nan);
  EXPECT_FALSE(nan < ics::Meters(0.0));
}

TEST(Units, ConvertsBetweenDegreesAndRadians) {
  EXPECT_DOUBLE_EQ(ics::to_radians(ics::Degrees(180.0)).value(), std::numbers::pi);
  EXPECT_DOUBLE_EQ(ics::to_radians(ics::Degrees(-90.0)).value(), -std::numbers::pi / 2.0);
  EXPECT_DOUBLE_EQ(ics::to_degrees(ics::Radians(std::numbers::pi / 4.0)).value(), 45.0);
  EXPECT_DOUBLE_EQ(ics::to_degrees(ics::to_radians(ics::Degrees(123.456))).value(), 123.456);
}

TEST(Units, WrapsAnglesIntoMinusPiToPi) {
  constexpr double kPi = std::numbers::pi;
  EXPECT_DOUBLE_EQ(ics::wrap_to_pi(ics::Radians(0.25)).value(), 0.25);
  EXPECT_DOUBLE_EQ(ics::wrap_to_pi(ics::Radians(kPi + 0.5)).value(), -kPi + 0.5);
  EXPECT_DOUBLE_EQ(ics::wrap_to_pi(ics::Radians(-kPi - 0.5)).value(), kPi - 0.5);
  EXPECT_NEAR(ics::wrap_to_pi(ics::Radians(7.0 * kPi + 1.0)).value(), -kPi + 1.0, 1e-12);
  EXPECT_TRUE(std::isnan(ics::wrap_to_pi(ics::Radians(std::numeric_limits<double>::infinity())).value()));
}

TEST(Units, UtcTimeRoundTripsTheNanosecondCount) {
  // 2026-10-01T00:00:00Z, which docs/frames-and-time.md counts in POSIX time.
  constexpr std::int64_t kUtcNs = 1'790'812'800'000'000'000;
  const ics::UtcTime time = ics::utc_from_ns(kUtcNs);
  EXPECT_EQ(ics::to_utc_ns(time), kUtcNs);
  const std::chrono::year_month_day date{std::chrono::floor<std::chrono::days>(time)};
  EXPECT_EQ(date, std::chrono::year{2026} / std::chrono::October / 1);
  EXPECT_EQ(ics::to_utc_ns(time + ics::Duration(5)), kUtcNs + 5);
  EXPECT_EQ(ics::to_utc_ns(ics::utc_from_ns(-1)), -1);
}

}  // namespace
