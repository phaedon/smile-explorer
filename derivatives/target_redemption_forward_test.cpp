#include "target_redemption_forward.h"

#include <gtest/gtest.h>

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
  const double expected_npv = 0.;
  const double error_threshold = 20000;

  TargetRedemptionForward tarf(1e6, 100e6, 135.657, 4.0, 0.25);

  for (int i = 0; i < 5; ++i) {
    double npv = tarf.price(125., 0.0004, 0.1, 10000);
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

  const double expected_npv = 50e6;
  const double error_threshold = 20000;

  TargetRedemptionForward tarf(1e6, 100e6, 131.9686, 4.0, 0.25);
  for (int i = 0; i < 5; ++i) {
    double npv = tarf.price(125., 0.0004, 0.1, 10000);
    EXPECT_LT(std::abs(npv - expected_npv), error_threshold);
  }
}

}  // namespace
}  // namespace smileexplorer
