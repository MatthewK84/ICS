#pragma once

#include <chrono>
#include <cmath>
#include <compare>
#include <cstdint>
#include <numbers>
#include <type_traits>

namespace ics {

// Strong unit types (ICS-016). A Quantity is a double tagged with its unit, so
// a length cannot be passed where an angle is expected, and a bare double
// cannot be passed as either. Only arithmetic that keeps the unit compiles:
// adding two lengths, scaling a length, or taking the ratio of two lengths.
// Converting between units is always a named function, such as to_radians.
template <typename Unit>
class Quantity {
 public:
  constexpr Quantity() noexcept = default;
  constexpr explicit Quantity(const double value) noexcept : value_(value) {}

  [[nodiscard]] constexpr double value() const noexcept { return value_; }

  // Written out rather than defaulted: Clang 17's coverage miscounts the
  // branches of a defaulted comparison, and the coverage gate needs them.
  [[nodiscard]] friend constexpr bool operator==(const Quantity a, const Quantity b) noexcept {
    return a.value_ == b.value_;
  }
  [[nodiscard]] friend constexpr std::partial_ordering operator<=>(const Quantity a, const Quantity b) noexcept {
    return a.value_ <=> b.value_;
  }

  [[nodiscard]] friend constexpr Quantity operator+(const Quantity a, const Quantity b) noexcept {
    return Quantity(a.value_ + b.value_);
  }
  [[nodiscard]] friend constexpr Quantity operator-(const Quantity a, const Quantity b) noexcept {
    return Quantity(a.value_ - b.value_);
  }
  [[nodiscard]] friend constexpr Quantity operator-(const Quantity a) noexcept { return Quantity(-a.value_); }
  [[nodiscard]] friend constexpr Quantity operator*(const Quantity a, const double scale) noexcept {
    return Quantity(a.value_ * scale);
  }
  [[nodiscard]] friend constexpr Quantity operator*(const double scale, const Quantity a) noexcept {
    return Quantity(scale * a.value_);
  }
  [[nodiscard]] friend constexpr Quantity operator/(const Quantity a, const double divisor) noexcept {
    return Quantity(a.value_ / divisor);
  }
  // The ratio of two quantities of the same unit has no unit.
  [[nodiscard]] friend constexpr double operator/(const Quantity a, const Quantity b) noexcept {
    return a.value_ / b.value_;
  }

 private:
  double value_{0.0};
};

// The units ICS uses, as tags. Each is declared and never defined.
namespace unit {
struct Meter;
struct Radian;
struct Degree;
}  // namespace unit

using Meters = Quantity<unit::Meter>;
using Radians = Quantity<unit::Radian>;
// Degrees appear only where ICS's conventions use them: geodetic latitude and
// longitude (docs/frames-and-time.md) and operator-facing config.
using Degrees = Quantity<unit::Degree>;

[[nodiscard]] constexpr Radians to_radians(const Degrees angle) noexcept {
  return Radians(angle.value() * (std::numbers::pi / 180.0));
}

[[nodiscard]] constexpr Degrees to_degrees(const Radians angle) noexcept {
  return Degrees(angle.value() * (180.0 / std::numbers::pi));
}

// The same angle in [-pi, pi]; NaN when the angle is not finite.
[[nodiscard]] inline Radians wrap_to_pi(const Radians angle) noexcept {
  return Radians(std::remainder(angle.value(), 2.0 * std::numbers::pi));
}

// Time (docs/frames-and-time.md): UTC as int64 nanoseconds since the Unix
// epoch, counted as POSIX time is, without leap seconds. std::chrono's
// system_clock counts the same way, so a UtcTime is a sys_time, and the
// arithmetic between times and durations is std::chrono's own.
using Duration = std::chrono::nanoseconds;
using UtcTime = std::chrono::sys_time<Duration>;
static_assert(std::is_same_v<Duration::rep, std::int64_t>, "a _ns field must fit a Duration exactly");

// The time in a field ending _utc_ns.
[[nodiscard]] constexpr UtcTime utc_from_ns(const std::int64_t utc_ns) noexcept { return UtcTime(Duration(utc_ns)); }

// The value for a field ending _utc_ns.
[[nodiscard]] constexpr std::int64_t to_utc_ns(const UtcTime time) noexcept { return time.time_since_epoch().count(); }

}  // namespace ics
