#include "rates/zero_curve.h"

#include <gtest/gtest.h>

#include <numbers>

namespace smileexplorer {
namespace {

TEST(RatesCurveTest, ZeroSpotCurve) {
  // Follows the example in Derman on pg. 486 (question 13-6)
  ZeroSpotCurve zeros(
      {1, 2, 3}, {0.05, 0.0747, 0.0992}, CompoundingPeriod::kAnnual);

  // Textbook numbers are rounded slightly; that's ok.
  EXPECT_NEAR(0.1, zeros.forwardRate(1, 2, CompoundingPeriod::kAnnual), 0.0001);
  EXPECT_NEAR(
      0.15, zeros.forwardRate(2, 3, CompoundingPeriod::kAnnual), 0.0002);

  // Ensure constant forwards past the end of the provided curve.
  EXPECT_NEAR(
      0.15, zeros.forwardRate(100, 200, CompoundingPeriod::kAnnual), 0.0002);

  EXPECT_DOUBLE_EQ(1.0, zeros.df(0.0));
  EXPECT_EQ(0, zeros.findClosestMaturityIndex(0.1));
  EXPECT_NEAR(std::exp(-0.05 * 0.1), zeros.df(0.1), 0.0005);

  for (double i = 0.0; i < 0.9; i += 0.1) {
    EXPECT_NEAR(0.05,
                zeros.forwardRate(i, i + 0.1, CompoundingPeriod::kAnnual),
                0.0005);
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
