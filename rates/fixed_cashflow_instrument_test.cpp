
#include "fixed_cashflow_instrument.h"

#include <gtest/gtest.h>

#include "rates/zero_curve.h"
#include "short_rate_tree_curve.h"

namespace smileexplorer {
namespace {

TEST(FixedIncomeInstrumentTest, ZeroCouponBond_FlatYieldCurve) {
  ZeroSpotCurve market_curve(
      {1, 2, 5, 10}, {.06, .06, .06, .06}, CompoundingPeriod::kAnnual);

  // TODO: It is a design flaw in the API that I have to specify dt twice. Fix
  // this.
  const double dt = 0.01;
  ShortRateTreeCurve short_rate_tree(
      HullWhitePropagator(0.1, 0.01, dt), market_curve, TrinomialTree(10, dt));

  FixedCashflowInstrument bond(&short_rate_tree);
  bond.setCashflows({Cashflow{
      .time_years = 5,
      .amount = 100,
  }});

  // TODO: This is not a very precise test -- the abs_error is rather high.
  // Investigate.
  EXPECT_NEAR(market_curve.df(5) * 100, bond.price(), 0.1);
}

TEST(FixedIncomeInstrumentTest, CouponBond_FlatYieldCurve) {
  ZeroSpotCurve market_curve(
      {1, 2, 5, 10}, {.06, .06, .06, .06}, CompoundingPeriod::kAnnual);

  // TODO: It is a design flaw in the API that I have to specify dt twice. Fix
  // this.
  const double dt = 0.01;
  ShortRateTreeCurve short_rate_tree(
      HullWhitePropagator(0.1, 0.01, dt), market_curve, TrinomialTree(5, dt));

  FixedCashflowInstrument bond(&short_rate_tree);
  // 3-year coupon bond with annual 6% coupons.
  bond.setCashflows({Cashflow{
                         .time_years = 3,
                         .amount = 100,
                     },
                     Cashflow{
                         .time_years = 1,
                         .amount = 6,
                     },
                     Cashflow{
                         .time_years = 2,
                         .amount = 6,
                     },
                     Cashflow{
                         .time_years = 3,
                         .amount = 6,
                     }});

  // TODO: Figure out why this isn't exactly 100.
  // This should price to par on a flat yield curve.
  EXPECT_NEAR(100, bond.price(), 0.05);
}

}  // namespace
}  // namespace smileexplorer
