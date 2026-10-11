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
#include "ics/mount/limits.hpp"
#include "ics/mount/pwi4_client.hpp"
#include "ics/mount/rate_loop.hpp"

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
  return Pwi4Settings{.host = "127.0.0.1",
                      .port = pwi4.port(),
                      .timeout = std::chrono::milliseconds(10),
                      .retry = {},
                      .limits = test_limits()};
}

inline Pwi4Client client_for(const EmulatedPwi4& pwi4) {
  auto client = Pwi4Client::make(settings_for(pwi4));
  return std::move(client.value());
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
