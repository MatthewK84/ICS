#include "ics/timealign/check.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <vector>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/units.hpp"
#include "ics/timealign/clock_fit.hpp"

namespace ics::timealign {
namespace {

constexpr std::int64_t kNsPerUs = 1'000;
constexpr double kNsPerUsDouble = 1'000.0;
constexpr double kPerMillion = 1e-6;

// The samples' mean offset (UTC minus boot time), and the largest distance
// of one from it.
struct Reference {
  std::int64_t offset_ns = 0;
  Nanoseconds spread{};
};

[[nodiscard]] std::int64_t offset_ns(const ClockSample& sample) noexcept {
  return sample.utc_ns - (sample.boot_us * kNsPerUs);
}

// The samples are in range and there is at least one: offsets fit in
// +-2100 years, and so do their differences and their mean.
[[nodiscard]] Reference reference(const std::span<const ClockSample> samples) {
  static_cast<void>(check(!samples.empty()));
  const std::int64_t first = offset_ns(samples.front());
  double sum = 0.0;
  for (const ClockSample& sample : samples) {
    sum += static_cast<double>(offset_ns(sample) - first);
  }
  const double mean = sum / static_cast<double>(samples.size());
  double spread = 0.0;
  for (const ClockSample& sample : samples) {
    spread = std::max(spread, std::abs(static_cast<double>(offset_ns(sample) - first) - mean));
  }
  return Reference{.offset_ns = first + std::llround(mean), .spread = Nanoseconds(spread)};
}

// The samples outside the withheld part of their span, with the drift put on
// their boot times.
[[nodiscard]] Result<std::vector<ClockSample>> injected(const std::span<const ClockSample> samples,
                                                        const Injection& injection, const Withholding& withholding) {
  const auto [lowest, highest] = std::ranges::minmax(samples, {}, &ClockSample::boot_us);
  const double span = static_cast<double>(highest.boot_us - lowest.boot_us);
  const double from = static_cast<double>(lowest.boot_us) + (withholding.from * span);
  const double to = static_cast<double>(lowest.boot_us) + (withholding.to * span);
  std::vector<ClockSample> out;
  for (const ClockSample& sample : samples) {
    const double boot = static_cast<double>(sample.boot_us);
    const std::optional<std::int64_t> boot_us = injection.apply(sample.boot_us);
    if (!boot_us) {
      return fail(Error::kInvalidArgument);
    }
    if (boot < from || boot > to) {
      out.push_back(ClockSample{.boot_us = *boot_us, .utc_ns = sample.utc_ns});
    }
  }
  return out;
}

// The largest difference between a position's time through the model and its
// reference.
[[nodiscard]] Result<Duration> max_error(const std::span<const std::int64_t> positions, const ClockModel& model,
                                         const Injection& injection, const Reference& truth) {
  Duration worst{};
  for (const std::int64_t boot_us : positions) {
    const std::optional<std::int64_t> drifted = injection.apply(boot_us);
    const std::optional<UtcTime> utc = drifted ? model.utc(*drifted) : std::nullopt;
    if (!utc || boot_us < 0 || boot_us > kMaxBootUs) {
      return fail(Error::kInvalidArgument);
    }
    const std::int64_t expected_ns = (boot_us * kNsPerUs) + truth.offset_ns;
    worst = std::max(worst, Duration(std::abs(to_utc_ns(*utc) - expected_ns)));
  }
  return worst;
}

}  // namespace

std::optional<std::int64_t> Injection::apply(const std::int64_t boot_us) const noexcept {
  const double offset_us = static_cast<double>(offset.count()) / kNsPerUsDouble;
  const double out = offset_us + (static_cast<double>(boot_us) * (1.0 + (ppm * kPerMillion)));
  if (!(out >= 0.0 && out <= static_cast<double>(kMaxBootUs))) {
    return std::nullopt;
  }
  return std::llround(out);
}

Result<DriftCheck> check_injected_drift(const std::span<const ClockSample> samples,
                                        const std::span<const std::int64_t> position_boot_us,
                                        const Injection& injection, const Withholding& withholding,
                                        const FitSettings& settings) {
  // Checking the samples as given first keeps reference() within range.
  const Result<ClockFit> given = fit_clock(samples, settings);
  if (!given) {
    return fail(given.error());
  }
  const Result<std::vector<ClockSample>> left = injected(samples, injection, withholding);
  Result<ClockFit> fit = left ? fit_clock(*left, settings) : fail(left.error());
  if (!fit) {
    return fail(fit.error());
  }
  const Reference truth = reference(samples);
  const Result<Duration> error = max_error(position_boot_us, fit->model, injection, truth);
  if (!error) {
    return fail(error.error());
  }
  return DriftCheck{
      .fit = *fit, .positions = position_boot_us.size(), .max_error = *error, .reference_spread = truth.spread};
}

}  // namespace ics::timealign
