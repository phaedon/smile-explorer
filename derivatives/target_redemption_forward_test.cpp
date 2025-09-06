#include "target_redemption_forward.h"

#include <gtest/gtest.h>

#include "rates/zero_curve.h"

namespace smileexplorer {
namespace {

TEST(TargetRedemptionForwardTest, AtmForward) {
  /*
  Verified in a spreadsheet:
   - expiry: 4 years
   - settlement frequency: quarterly (0.25)
   - underlying: USD-ISK
   - spot fx rate: 125.0
   - USD (foreign) rate: 4%
   - ISK (domestic) rate: 8%
   - notional: $1,000,000

   At this interest rate differential, the discount-weighted average of the
  forwards is 135.657.

  With a high-enough target (here, 100mm ISK) and a very low vol, this is just a
  strip of forwards. For reference, the accumulated profit of the profitable
  forwards would be 48mm ISK.
  */
  ZeroSpotCurve domestic_curve(
      {1, 3}, {0.08, 0.08}, CompoundingPeriod::kContinuous);
  ZeroSpotCurve foreign_curve(
      {1, 3}, {0.04, 0.04}, CompoundingPeriod::kContinuous);

  const double expected_npv = 0.;
  const double error_threshold = 20000;

  TargetRedemptionForward tarf(1e6, 100e6, 135.657, 4.0, 0.25);

  for (int i = 0; i < 5; ++i) {
    double npv =
        tarf.price(125., 0.0004, 0.1, 10000, foreign_curve, domestic_curve);
    EXPECT_LT(std::abs(npv - expected_npv), error_threshold);
  }
}

TEST(TargetRedemptionForwardTest, OtmForward) {
  /*
  Similar to the ATM example, but by setting the strike away from the wtd avg
  forward, we have a nonzero NPV. This is to ensure that the order-of-magnitude
  of the NPV computations is correct.

  At a strike of 131.9686 and the other params kept the same, this should result
  in an NPV of almost exactly 50mm ISK.
  */

  ZeroSpotCurve domestic_curve(
      {1, 3}, {0.08, 0.08}, CompoundingPeriod::kContinuous);
  ZeroSpotCurve foreign_curve(
      {1, 3}, {0.04, 0.04}, CompoundingPeriod::kContinuous);

  const double expected_npv = 50e6;
  const double error_threshold = 20000;

  TargetRedemptionForward tarf(1e6, 100e6, 131.9686, 4.0, 0.25);
  for (int i = 0; i < 5; ++i) {
    double npv =
        tarf.price(125., 0.0004, 0.1, 10000, foreign_curve, domestic_curve);

    EXPECT_LT(std::abs(npv - expected_npv), error_threshold);
  }
}

TEST(TargetRedemptionForwardTest, KnockoutAlmostDeterministic) {
  /*
  Similar to the AtmForward case above, but with a 6mm ISK cumulative profit
  target. At a very low vol, this is almost certain to happen at year 2.75 (with
  positive payments at t=[2.25, 2.5, 2.75]).

  The expected NPV is very negative here, because we are using the ATM fwd as
  the strike, yet we have a fairly low profit target and a large interest-rate
  differential. Therefore, there are large accumulated losses in the first 2
  years (around -39mm ISK, not discounted) and only +6mm ISK worth of profits
  due to the fixed target.
  */

  ZeroSpotCurve domestic_curve(
      {1, 3}, {0.08, 0.08}, CompoundingPeriod::kContinuous);
  ZeroSpotCurve foreign_curve(
      {1, 3}, {0.04, 0.04}, CompoundingPeriod::kContinuous);

  // This is the discounted amount of the approx. -33mm in total losses (-39mm +
  // 6mm):
  const double expected_npv = -31.75e6;
  const double error_threshold = 20000;

  TargetRedemptionForward tarf(1e6, 6e6, 135.657, 4.0, 0.25);

  for (int i = 0; i < 5; ++i) {
    double npv =
        tarf.price(125., 0.0004, 0.1, 10000, foreign_curve, domestic_curve);
    EXPECT_LT(std::abs(npv - expected_npv), error_threshold);
  }
}

}  // namespace
}  // namespace smileexplorer
