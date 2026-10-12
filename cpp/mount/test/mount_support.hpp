#pragma once

#include <chrono>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <utility>

#include "emulated_pwi4.hpp"
#include "ics/common/units.hpp"
#include "ics/logging/logger.hpp"
#include "ics/frames/geodetic.hpp"
#include "ics/mount/interlock.hpp"
#include "ics/mount/limits.hpp"
#include "ics/mount/pwi4_client.hpp"
#include "ics/mount/rate_loop.hpp"
#include "ics/mount/sun.hpp"

namespace ics::mount::testing {

inline constexpr double kDeg = std::numbers::pi / 180.0;

// Azimuth within ±260° and altitude 5° to 85°, inside the emulator's
// mechanical limits; both axes at most 10°/s.
inline MountLimits test_limits() {
  MountLimits out;
  out.axes[0] = {.min = Radians(-260 * kDeg), .max = Radians(260 * kDeg), .max_rate_rad_per_s = 10 * kDeg};
  out.axes[1] = {.min = Radians(5 * kDeg), .max = Radians(85 * kDeg), .max_rate_rad_per_s = 10 * kDeg};
  return out;
}

// A client of the emulator, with the default timeout and retries.
inline Pwi4Settings settings_for(const EmulatedPwi4& pwi4) {
  return Pwi4Settings{.port = pwi4.port(),
                      .timeout = std::chrono::milliseconds(10),
                      .retry = {},
                      .limits = test_limits()};
}

inline Pwi4Client client_for(const EmulatedPwi4& pwi4) {
  auto client = Pwi4Client::make(settings_for(pwi4));
  return std::move(client.value());
}

// The interlock's clock for the tests (ICS-034): UTC from start, running on
// with the steady clock, and a healthy report each time it is asked, unless
// a test says otherwise.
class TestClock final : public ClockSource {
 public:
  explicit TestClock(const UtcTime start) : start_(start), steady_start_(std::chrono::steady_clock::now()) {}
  [[nodiscard]] UtcTime now() override { return start_ + (std::chrono::steady_clock::now() - steady_start_) + jump_; }
  [[nodiscard]] std::optional<ClockHealth> health() override {
    if (!reporting_) {
      return std::nullopt;
    }
    return ClockHealth{.traceable = traceable_, .error_bound = std::chrono::microseconds(1), .reported = now()};
  }
  // From now on: no reports, or reports that the clock is free-running, or
  // UTC moved on by jump.
  void stop_reporting() { reporting_ = false; }
  void free_running() { traceable_ = false; }
  void jump(const Duration by) { jump_ = by; }

 private:
  UtcTime start_;
  std::chrono::steady_clock::time_point steady_start_;
  Duration jump_{};
  bool reporting_ = true;
  bool traceable_ = true;
};

// A site like a southwestern US range's.
inline frames::Geodetic bench_site() {
  return frames::Geodetic::make(Degrees(32.38), Degrees(-106.48), Meters(1200.0)).value();
}

// 01:00 local time on 22 June 2026 at the bench site: the sun far below the
// horizon.
inline UtcTime night() { return utc_from_ns(1782111600000000000); }

// The interlock's settings for the tests: parked due north, 10° up.
inline InterlockSettings interlock_settings() {
  InterlockSettings out;
  out.park = {.altitude = Radians(10 * kDeg), .azimuth = Radians(0.0)};
  return out;
}

// An interlock around a client of the emulator, whose sun it places where
// the interlock will work it out to be.
inline SunInterlock interlock_for(EmulatedPwi4& pwi4, TestClock& clock, const Pwi4Settings& settings) {
  pwi4.set_sun(sun_position(clock.now(), bench_site(), SunSettings{}).value());
  auto client = Pwi4Client::make(settings);
  auto interlock = SunInterlock::make(std::move(client.value()), clock, bench_site(), interlock_settings());
  return std::move(interlock.value());
}

inline SunInterlock interlock_for(EmulatedPwi4& pwi4, TestClock& clock) {
  return interlock_for(pwi4, clock, settings_for(pwi4));
}

// A second from now: room for every retry.
inline Deadline soon() { return std::chrono::steady_clock::now() + std::chrono::seconds(1); }

// The same demand every time, or none.
class FixedSource final : public RateSource {
 public:
  explicit FixedSource(std::optional<RateDemand> demand) : demand_(std::move(demand)) {}
  [[nodiscard]] std::optional<RateDemand> latest() override { return demand_; }

 private:
  std::optional<RateDemand> demand_;
};

// A demand made now, of these rates in degrees per second.
inline RateDemand fresh(const double az_deg_s, const double alt_deg_s) {
  return {.rates = {.rad_per_s = {az_deg_s * kDeg, alt_deg_s * kDeg}}, .time = logging::system_now()};
}

// A moving demand, as a tracker following a target makes it: azimuth swings
// at up to 5°/s and altitude at up to 2°/s, over a 4 s cycle.
class SweepSource final : public RateSource {
 public:
  [[nodiscard]] std::optional<RateDemand> latest() override {
    const UtcTime now = logging::system_now();
    const double phase = 2.0 * std::numbers::pi * std::chrono::duration<double>(now.time_since_epoch()).count() / 4.0;
    return RateDemand{.rates = {.rad_per_s = {5 * kDeg * std::sin(phase), 2 * kDeg * std::cos(phase)}}, .time = now};
  }
};

}  // namespace ics::mount::testing
