#pragma once

#include <optional>

#include "ics/common/error.hpp"

namespace ics::cot {

// CoT's position errors as one sigma (ICS-022). A CoT point's ce is the radius
// of a circle, and its le the half-height of an interval about hae, that hold
// the true position with some probability. CoT tools disagree on that
// probability, so the adapter's settings give it, 0.90 by default (CE90 and
// LE90). Each conversion assumes a normal error: circular for ce, so that
// sigma = ce / sqrt(-2 ln(1 - p)), the Rayleigh quantile, and one-dimensional
// for le, so that sigma = le / z, where z is the standard normal quantile of
// (1 + p) / 2.

// CoT's value for a height or error it does not know.
inline constexpr double kUnknown = 9999999.0;

// The factors that turn ce and le into one sigma: sigma = ce / ce_factor.
struct SigmaFactors {
  double ce_factor = 0.0;
  double le_factor = 0.0;
};

// The factors for ce and le given at these probabilities. Fails with
// Error::kInvalidArgument unless each probability is strictly between 0 and 1.
[[nodiscard]] Result<SigmaFactors> sigma_factors(double ce_probability, double le_probability) noexcept;

// The standard normal quantile of q, for q in [0.5, 1): the z with
// P(Z <= z) = q. Accurate to about 1e-15.
[[nodiscard]] double normal_quantile(double q) noexcept;

// One sigma for a CoT error value and its factor: nothing for CoT's unknown
// value, a negative value, or one that is not finite.
[[nodiscard]] std::optional<double> sigma(double error, double factor) noexcept;

}  // namespace ics::cot
