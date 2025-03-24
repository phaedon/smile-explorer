
#include "fixed_cashflow_instrument.h"

#include <gtest/gtest.h>

#include "rates/short_rate_tree_curve.h"
#include "rates/zero_curve.h"

namespace smileexplorer {
namespace {

TEST(FixedIncomeInstrumentTest, ZeroCouponBond_FlatYieldCurve) {
  ZeroSpotCurve market_curve(
      {1, 2, 5, 10}, {.06, .06, .06, .06}, CompoundingPeriod::kAnnual);

  ShortRateTreeCurve short_rate_tree(
      std::make_unique<HullWhitePropagator>(0.1, 0.01, .01),
      market_curve,
      10.0);

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

  ShortRateTreeCurve short_rate_tree(
      std::make_unique<HullWhitePropagator>(0.1, 0.01, .01), market_curve, 5.0);

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
