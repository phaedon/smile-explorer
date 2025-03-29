
#include "fixed_cashflow_instrument.h"

#include <gtest/gtest.h>

#include "gmock/gmock.h"
#include "rates/short_rate_tree_curve.h"
#include "rates/zero_curve.h"

namespace smileexplorer {
namespace {

using testing::HasSubstr;

TEST(FixedCashflowInstrumentTest, ZeroCouponBond_FlatYieldCurve) {
  ZeroSpotCurve market_curve(
      {1, 2, 5, 10}, {.06, .06, .06, .06}, CompoundingPeriod::kAnnual);

  ShortRateTreeCurve short_rate_tree(
      std::make_unique<HullWhitePropagator>(0.1, 0.01, .01),
      market_curve,
      10.0);

  FixedCashflowInstrument bond(&short_rate_tree);
  auto status = bond.setCashflows({Cashflow{
      .time_years = 5,
      .amount = 100,
  }});

  ASSERT_EQ(status, absl::OkStatus());
  EXPECT_NEAR(market_curve.df(5) * 100, bond.price(), 0.001);
}

TEST(FixedCashflowInstrumentTest, CouponBond_FlatYieldCurve) {
  ZeroSpotCurve market_curve(
      {1, 2, 5, 10}, {.06, .06, .06, .06}, CompoundingPeriod::kAnnual);

  ShortRateTreeCurve short_rate_tree(
      std::make_unique<HullWhitePropagator>(0.1, 0.01, .01), market_curve, 5.0);

  FixedCashflowInstrument bond(&short_rate_tree);
  // 3-year coupon bond with annual 6% coupons.
  auto status = bond.setCashflows({Cashflow{
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

  ASSERT_EQ(status, absl::OkStatus());
  EXPECT_NEAR(100, bond.price(), 0.001);
}

TEST(FixedCashflowInstrumentTest, BadCashflowDates) {
  ZeroSpotCurve market_curve(
      {1, 2, 5, 10}, {.06, .06, .06, .06}, CompoundingPeriod::kAnnual);
  ShortRateTreeCurve short_rate_tree(
      std::make_unique<HullWhitePropagator>(0.1, 0.01, .01), market_curve, 2.0);
  FixedCashflowInstrument bond(&short_rate_tree);
  auto status =
      bond.addCashflowToTree(Cashflow{.time_years = 2.2, .amount = 1});
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code());

  auto multi_cashflow_status = bond.setCashflows({Cashflow{
                                                      .time_years = 0.5,
                                                      .amount = 1,
                                                  },
                                                  Cashflow{
                                                      .time_years = 1.0,
                                                      .amount = 2,
                                                  },
                                                  Cashflow{
                                                      .time_years = 2.0,
                                                      .amount = 6,
                                                  },
                                                  Cashflow{
                                                      .time_years = 3.0,
                                                      .amount = 2,
                                                  },
                                                  Cashflow{
                                                      .time_years = 4.0,
                                                      .amount = 6,
                                                  }});

  EXPECT_EQ(multi_cashflow_status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(multi_cashflow_status.message(), HasSubstr("2 of 5"));
}

}  // namespace
}  // namespace smileexplorer
