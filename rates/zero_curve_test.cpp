#include "rates/zero_curve.h"

#include <gtest/gtest.h>

#include <numbers>

namespace smileexplorer {
namespace {

TEST(RatesCurveTest, ZeroSpotCurve) {
  // Follows the example in Derman on pg. 486 (question 13-6). But we compute
  // them exactly to ensure tight tolerance.
  constexpr double tolerance = 0.00001;

  constexpr double fwd_1x2 = 0.1;
  constexpr double fwd_2x3 = 0.15;

  const double spot_0x1 = 0.05;

  // approx 0.074709
  const double spot_0x2 = std::sqrt((1 + spot_0x1) * (1 + fwd_1x2)) - 1;

  // approx 0.0992419
  const double spot_0x3 =
      std::pow((1 + spot_0x1) * (1 + fwd_1x2) * (1 + fwd_2x3), 1 / 3.) - 1;

  ZeroSpotCurve zeros(
      {1, 2, 3}, {spot_0x1, spot_0x2, spot_0x3}, CompoundingPeriod::kAnnual);

  EXPECT_DOUBLE_EQ(1.0, zeros.df(0.0));

  EXPECT_NEAR(
      fwd_1x2, zeros.forwardRate(1, 2, CompoundingPeriod::kAnnual), tolerance);
  EXPECT_NEAR(
      fwd_2x3, zeros.forwardRate(2, 3, CompoundingPeriod::kAnnual), tolerance);

  // Ensure constant forwards past the end of the provided curve.
  EXPECT_NEAR(fwd_2x3,
              zeros.forwardRate(100, 200, CompoundingPeriod::kAnnual),
              tolerance);

  for (double i = 0.0; i < 0.9; i += 0.1) {
    EXPECT_NEAR(spot_0x1,
                zeros.forwardRate(i, i + 0.1, CompoundingPeriod::kAnnual),
                tolerance);
  }
}

TEST(ZeroCurveTest, DoublePrecisionDivision) {
  EXPECT_TRUE(isIntegerMultipleOf(30 + (1 / 365.), (1 / 365.)));
  EXPECT_FALSE(isIntegerMultipleOf(30 + (1.1 / 365.), (1 / 365.)));

  // Try with some known irrational numbers.
  EXPECT_TRUE(
      isIntegerMultipleOf(54321 * std::numbers::phi, std::numbers::phi));
  EXPECT_TRUE(
      isIntegerMultipleOf(12345 * std::numbers::sqrt3, std::numbers::sqrt3));

  // Verify that the accumulated floating-point error is still ok.
  const double small_timestep = 1. / 365.;
  double accum_timesteps = 0;
  for (int i = 0; i < 40 * 365; ++i) {
    accum_timesteps += small_timestep;
  }
  EXPECT_TRUE(isIntegerMultipleOf(accum_timesteps, small_timestep));
}

TEST(ZeroCurveTest, CheckAllSpacings) {
  EXPECT_TRUE(areAllSpacingsIntegerMultiplesOf(
      {0, 0.25, 0.75, 1.75, 2.0, 10.25}, 0.25));

  // Small tweaks cause failure.
  EXPECT_FALSE(areAllSpacingsIntegerMultiplesOf(
      {0, 0.2501, 0.75, 1.75, 2.0, 10.25}, 0.25));
  EXPECT_FALSE(areAllSpacingsIntegerMultiplesOf(
      {0, 0.25, 0.75, 1.75, 2.0, 10.25}, 0.25001));

  // Yes ok, but not sorted.
  EXPECT_FALSE(
      areAllSpacingsIntegerMultiplesOf({0, .75, 0.25, 1.75, 2.0, 10.25}, 0.25));

  // Accumulation of rational numbers like 1/12.
  std::vector<double> twelfths{0};
  for (size_t i = 1; i < 12 * 40; ++i) {
    // We don't just do i/12. because we want to enable the possibility of too
    // much accumulated error.
    twelfths.push_back(twelfths[i - 1] + 1 / 12.);
  }
  EXPECT_TRUE(areAllSpacingsIntegerMultiplesOf(twelfths, 1 / 12.));

  // Don't accept two identical consecutive entries.
  EXPECT_FALSE(areAllSpacingsIntegerMultiplesOf({0, 1, 1, 2}, 1));
}

}  // namespace
}  // namespace smileexplorer
