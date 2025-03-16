#include "rates_curve.h"

#include <gtest/gtest.h>

#include <cmath>

#include "short_rate_tree_curve.h"

namespace smileexplorer {
namespace {

TEST(RatesCurveTest, TreeCalibrationMatchesMarketRates) {
  constexpr double tolerance = 0.0001;
  ZeroSpotCurve market_curve({1, 2, 5, 10}, {.03, .035, .04, .045});

  // TODO: See if it is possible to increase the grid spacing while still
  // getting within a 1bps tolerance of market rate fitting.
  constexpr double dt = 1. / 80;
  TrinomialTree tree(10, 0.1, dt, 0.05);
  tree.forwardPropagate(market_curve);
  ShortRateTreeCurve tree_curve(std::move(tree));

  for (double i = 0; i < 10; i += 1) {
    for (double j = i + 0.25; j <= i + 5; j += 0.25) {
      const double mkt_fwd =
          market_curve.forwardRate(i, j, CompoundingPeriod::kQuarterly);
      const double tree_fwd =
          tree_curve.forwardRate(i, j, CompoundingPeriod::kQuarterly);
      EXPECT_NEAR(mkt_fwd, tree_fwd, tolerance);
    }
  }
}

TEST(RatesCurveTest, ForwardRatesAndDiscountFactorsMatch) {
  double expected_rate = 0.031415;
  double tenor = 2.718;
  for (const auto period : {CompoundingPeriod::kAnnual,
                            CompoundingPeriod::kSemi,
                            CompoundingPeriod::kQuarterly,
                            CompoundingPeriod::kMonthly,
                            CompoundingPeriod::kContinuous}) {
    double df_end = dfByPeriod(expected_rate, tenor, period);
    double fwdrate = fwdRateByPeriod(1.0, df_end, tenor, period);
    EXPECT_DOUBLE_EQ(expected_rate, fwdrate);
  }
}

}  // namespace
}  // namespace smileexplorer
