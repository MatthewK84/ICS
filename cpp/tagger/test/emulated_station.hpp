#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>

namespace ics::tagger::testing {

// A station in software for the time-tagger's bench (ICS-032): the true UTC,
// the tagger's 32-bit counter running off a crystal that is not on its
// nominal rate and wanders, the GNSS PPS edge with its jitter, and a mount
// turning in azimuth. Times are true UTC in ns since the start of second
// kStartSecond.
class EmulatedStation {
 public:
  static constexpr std::int64_t kStartSecond = 1'790'000'000;
  static constexpr std::uint64_t kNominalHz = 200'000'000;
  // The counter's phase at the start, in ticks.
  static constexpr double kPhaseTicks = 123'456'789.0;
  // The encoders' counts per revolution.
  static constexpr std::uint32_t kCountsPerRev = 1U << 24U;
  // Azimuth turns at 0.1 rad/s from 0; elevation holds 0.3 rad.
  static constexpr double kAzimuthRate = 0.1;
  static constexpr double kElevation = 0.3;

  // A crystal offset_ppm from nominal that wanders by wander_ppm_per_s, and
  // PPS edges with jitter_ns of Gaussian jitter.
  EmulatedStation(const double offset_ppm, const double wander_ppm_per_s, const double jitter_ns,
                  const std::uint64_t seed)
      : offset_ppm_(offset_ppm), wander_ppm_per_s_(wander_ppm_per_s), jitter_(0.0, jitter_ns), random_(seed) {}

  // The raw 32-bit counter at true time t_ns.
  [[nodiscard]] std::uint32_t counter(const std::int64_t t_ns) const noexcept {
    const double t = static_cast<double>(t_ns) / 1e9;
    // The integral of the rate nominal × (1 + (offset + wander × t) × 1e-6).
    const double ppm_seconds = (offset_ppm_ * t) + (0.5 * wander_ppm_per_s_ * t * t);
    const double ticks = kPhaseTicks + (static_cast<double>(kNominalHz) * (t + (ppm_seconds * 1e-6)));
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(std::llround(ticks)));
  }

  // When the PPS edge of the second starting at t_ns really comes, with its
  // jitter.
  [[nodiscard]] std::int64_t pps_edge(const std::int64_t t_ns) { return t_ns + std::llround(jitter_(random_)); }

  [[nodiscard]] static std::int32_t azimuth_counts(const std::int64_t t_ns) noexcept {
    return counts(kAzimuthRate * static_cast<double>(t_ns) / 1e9);
  }
  [[nodiscard]] static std::int32_t elevation_counts() noexcept { return counts(kElevation); }

 private:
  [[nodiscard]] static std::int32_t counts(const double rad) noexcept {
    return static_cast<std::int32_t>(std::llround(rad * kCountsPerRev / (2.0 * std::numbers::pi)));
  }

  double offset_ppm_;
  double wander_ppm_per_s_;
  std::normal_distribution<double> jitter_;
  std::mt19937_64 random_;
};

}  // namespace ics::tagger::testing
