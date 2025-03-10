#include "vanilla_option.h"

#include <gtest/gtest.h>
namespace smileexplorer {
namespace {

TEST(VanillaOptionTest, CurrencyOptions) {
  ZeroSpotCurve domestic_curve(
      {1, 3}, {0.08, 0.08}, CompoundingPeriod::kContinuous);
  ZeroSpotCurve foreign_curve(
      {1, 3}, {0.04, 0.04}, CompoundingPeriod::kContinuous);

  double spot = 140;
  double strike = 150;
  double vol = 0.2;
  double expiry = 1.0;

  VanillaOption vanilla(strike, OptionPayoff::Call);
  // Expected values extracted from
  // http://www.finance-calculators.com/fxoptions/
  EXPECT_NEAR(
      9.007,
      vanilla.blackScholes(spot, vol, expiry, foreign_curve, domestic_curve),
      0.0005);
  EXPECT_NEAR(
      0.46317,
      vanilla.blackScholesGreek(
          spot, vol, expiry, foreign_curve, domestic_curve, Greeks::Delta),
      0.000005);
  EXPECT_NEAR(
      0.53608,
      vanilla.blackScholesGreek(
          spot, vol, expiry, foreign_curve, domestic_curve, Greeks::Vega),
      0.000005);
  EXPECT_NEAR(
      0.01368,
      vanilla.blackScholesGreek(
          spot, vol, expiry, foreign_curve, domestic_curve, Greeks::Gamma),
      0.000005);
}

}  // namespace
}  // namespace smileexplorer
