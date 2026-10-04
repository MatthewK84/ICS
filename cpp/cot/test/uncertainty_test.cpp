#include "ics/cot/uncertainty.hpp"

#include <cmath>
#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include "ics/common/error.hpp"

namespace ics::cot {
namespace {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();

TEST(NormalQuantile, MatchesTheTabulatedValues) {
  // Python's statistics.NormalDist().inv_cdf, in the central region and the tail.
  EXPECT_NEAR(normal_quantile(0.5), 0.0, 1e-15);
  EXPECT_NEAR(normal_quantile(0.95), 1.6448536269514715, 1e-14);
  EXPECT_NEAR(normal_quantile(0.975), 1.9599639845400536, 1e-14);
  EXPECT_NEAR(normal_quantile(0.97575), 1.9729610513118847, 1e-14);
  EXPECT_NEAR(normal_quantile(0.995), 2.5758293035489, 1e-14);
  EXPECT_NEAR(normal_quantile(0.999999), 4.753424308817089, 1e-12);
}

TEST(SigmaFactors, AreTheRayleighAndNormalQuantiles) {
  const Result<SigmaFactors> ce90 = sigma_factors(0.90, 0.90);
  ASSERT_TRUE(ce90.has_value());
  EXPECT_NEAR(ce90->ce_factor, 2.145966026289347, 1e-14);
  EXPECT_NEAR(ce90->le_factor, 1.6448536269514715, 1e-14);
  const Result<SigmaFactors> ce95 = sigma_factors(0.95, 0.95);
  ASSERT_TRUE(ce95.has_value());
  EXPECT_NEAR(ce95->ce_factor, 2.4477468306808166, 1e-14);
  EXPECT_NEAR(ce95->le_factor, 1.9599639845400536, 1e-14);
  // CEP, a 50% circle.
  EXPECT_NEAR(sigma_factors(0.5, 0.5)->ce_factor, 1.1774100225154747, 1e-14);
}

TEST(SigmaFactors, RejectAProbabilityOutsideZeroToOne) {
  for (const double bad : {0.0, 1.0, -0.1, 1.5, kNan}) {
    EXPECT_EQ(sigma_factors(bad, 0.9).error(), Error::kInvalidArgument) << bad;
    EXPECT_EQ(sigma_factors(0.9, bad).error(), Error::kInvalidArgument) << bad;
  }
}

TEST(Sigma, DividesAKnownErrorByItsFactor) {
  EXPECT_DOUBLE_EQ(sigma(4.0, 2.0).value_or(0.0), 2.0);
  EXPECT_DOUBLE_EQ(sigma(0.0, 2.0).value_or(1.0), 0.0);
  EXPECT_DOUBLE_EQ(sigma(9999998.0, 1.0).value_or(0.0), 9999998.0);
}

TEST(Sigma, IsNothingForAnUnknownOrImpossibleError) {
  for (const double error : {kUnknown, 1e9, -1.0, kNan, kInfinity}) {
    EXPECT_EQ(sigma(error, 2.0), std::nullopt) << error;
  }
}

}  // namespace
}  // namespace ics::cot
