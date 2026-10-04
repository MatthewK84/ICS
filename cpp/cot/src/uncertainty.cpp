#include "ics/cot/uncertainty.hpp"

#include <array>
#include <cmath>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>

#include "ics/common/check.hpp"
#include "ics/common/error.hpp"

namespace ics::cot {
namespace {

// Acklam's rational approximation to the standard normal quantile, in its
// central region and upper tail (P. J. Acklam, 2003; relative error below
// 1.2e-9). Coefficients run from the highest power down.
constexpr double kTailStart = 0.97575;
constexpr std::array<double, 6> kCentralNumerator{-3.969683028665376e+01, 2.209460984245205e+02,
                                                  -2.759285104469687e+02, 1.383577518672690e+02,
                                                  -3.066479806614716e+01, 2.506628277459239e+00};
constexpr std::array<double, 6> kCentralDenominator{-5.447609879822406e+01, 1.615858368580409e+02,
                                                    -1.556989798598866e+02, 6.680131188771972e+01,
                                                    -1.328068155288572e+01, 1.0};
constexpr std::array<double, 6> kTailNumerator{-7.784894002430293e-03, -3.223964580411365e-01,
                                               -2.400758277161838e+00, -2.549732539343734e+00,
                                               4.374664141464968e+00,  2.938163982698783e+00};
constexpr std::array<double, 5> kTailDenominator{7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                                                 3.754408661907416e+00, 1.0};

// The polynomial with these coefficients, highest power first, at x.
[[nodiscard]] double polynomial(const std::span<const double> coefficients, const double x) noexcept {
  return std::accumulate(coefficients.begin(), coefficients.end(), 0.0,
                         [x](const double sum, const double coefficient) { return (sum * x) + coefficient; });
}

[[nodiscard]] double acklam(const double q) noexcept {
  if (q <= kTailStart) {
    const double r = q - 0.5;
    return polynomial(kCentralNumerator, r * r) * r / polynomial(kCentralDenominator, r * r);
  }
  const double t = std::sqrt(-2.0 * std::log(1.0 - q));
  return -polynomial(kTailNumerator, t) / polynomial(kTailDenominator, t);
}

[[nodiscard]] bool is_probability(const double p) noexcept { return p > 0.0 && p < 1.0; }

}  // namespace

double normal_quantile(const double q) noexcept {
  static_cast<void>(check(q >= 0.5));
  static_cast<void>(check(q < 1.0));
  // One step of Halley's method against the exact normal distribution takes
  // Acklam's approximation to full double precision. Its error, Phi(x) - q,
  // is taken between the upper tails, as 1 - q is exact for q of at least
  // 0.5: taken near 1 it would lose all but a few digits deep in the tail.
  const double x = acklam(q);
  const double error = (1.0 - q) - (0.5 * std::erfc(x / std::numbers::sqrt2));
  const double u = error * std::sqrt(2.0 * std::numbers::pi) * std::exp(x * x / 2.0);
  return x - (u / (1.0 + (x * u / 2.0)));
}

Result<SigmaFactors> sigma_factors(const double ce_probability, const double le_probability) noexcept {
  if (!is_probability(ce_probability) || !is_probability(le_probability)) {
    return fail(Error::kInvalidArgument);
  }
  return SigmaFactors{.ce_factor = std::sqrt(-2.0 * std::log(1.0 - ce_probability)),
                      .le_factor = normal_quantile((1.0 + le_probability) / 2.0)};
}

std::optional<double> sigma(const double error, const double factor) noexcept {
  static_cast<void>(check(factor > 0.0));
  if (!std::isfinite(error) || error < 0.0 || error >= kUnknown) {
    return std::nullopt;
  }
  return error / factor;
}

}  // namespace ics::cot
