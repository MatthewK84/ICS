// Fuzzes the clock fit and the drift check (ICS-026) with any samples. The
// first four bytes choose the settings, an injection and a withholding; the
// rest is pairs of little-endian int64 boot and UTC times. Neither may
// overflow or crash, and each must keep its promises:
// - a fit uses at least two samples, and counts every sample as used or
//   rejected, with residuals that are finite and not negative;
// - a drift check measures every position, with an error that is not
//   negative.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <span>
#include <vector>

#include "ics/common/error.hpp"
#include "ics/timealign/check.hpp"
#include "ics/timealign/clock_fit.hpp"

namespace {

using ics::timealign::ClockSample;

constexpr std::size_t kHeader = 4;
constexpr std::size_t kPair = 2 * sizeof(std::int64_t);
constexpr std::size_t kMaxSamples = 512;

void require(const bool condition) {
  if (!condition) {
    std::abort();
  }
}

std::int64_t int64_at(const std::span<const std::uint8_t> bytes) {
  std::int64_t value = 0;
  std::memcpy(&value, bytes.data(), sizeof(value));
  return value;
}

std::vector<ClockSample> samples_of(const std::span<const std::uint8_t> bytes) {
  std::vector<ClockSample> out;
  for (std::size_t at = 0; at + kPair <= bytes.size() && out.size() < kMaxSamples; at += kPair) {
    out.push_back(ClockSample{.boot_us = int64_at(bytes.subspan(at)),
                              .utc_ns = int64_at(bytes.subspan(at + sizeof(std::int64_t)))});
  }
  return out;
}

void require_sound(const ics::timealign::ClockFit& fit, const std::size_t count) {
  require(fit.used >= 2 && fit.used + fit.rejected == count);
  require(std::isfinite(fit.residual_rms.count()) && fit.residual_rms.count() >= 0.0);
  require(std::isfinite(fit.residual_max.count()) && fit.residual_max.count() >= 0.0);
  require(fit.first_boot_us <= fit.last_boot_us);
  require(ics::timealign::straight(fit) == (fit.residual_rms <= ics::timealign::kStraightRms));
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> input(data, size);
  if (input.size() < kHeader) {
    return 0;
  }
  const ics::timealign::FitSettings settings{.max_iterations = input[0] % 8U, .min_samples = input[1] % 8U};
  const std::vector<ClockSample> samples = samples_of(input.subspan(kHeader));
  const ics::Result<ics::timealign::ClockFit> fit = ics::timealign::fit_clock(samples, settings);
  if (fit) {
    require_sound(*fit, samples.size());
  }
  std::vector<std::int64_t> positions;
  std::ranges::transform(samples, std::back_inserter(positions), &ClockSample::boot_us);
  const ics::timealign::Injection injection{.ppm = static_cast<double>(static_cast<std::int8_t>(input[2])) * 2.0,
                                            .offset = std::chrono::seconds(input[3] % 16U)};
  const ics::timealign::Withholding withholding{.from = 0.25, .to = static_cast<double>(input[3] / 16U) / 16.0};
  const ics::Result<ics::timealign::DriftCheck> check =
      ics::timealign::check_injected_drift(samples, positions, injection, withholding, settings);
  if (check) {
    require(check->positions == positions.size() && check->max_error.count() >= 0);
    require(check->reference_spread.count() >= 0.0);
    require_sound(check->sent, samples.size());
    const ics::timealign::Outcome outcome = ics::timealign::judge(*check);
    require((outcome == ics::timealign::Outcome::kNotStraight) == !ics::timealign::straight(check->sent));
  }
  return 0;
}
