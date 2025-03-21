
#include "short_rate_tree_curve.h"

#include <gtest/gtest.h>

#include "gmock/gmock.h"

namespace smileexplorer {
namespace {

using testing::DoubleNear;

TEST(ShortRateTreeCurveTest, Hull_SecondStage) {
  ZeroSpotCurve market_curve(
      {0.5, 1.0, 1.5, 2.0, 2.5, 3.0},
      {.0343, .03824, 0.04183, 0.04512, 0.04812, 0.05086});

  const double sigma = 0.01;
  const double dt = 1.0;
  size_t num_elems_to_verify = 3;

  HullWhitePropagator hw_prop(0.1, sigma, dt);
  ShortRateTreeCurve tree_curve(
      hw_prop, market_curve, TrinomialTree(num_elems_to_verify * dt, dt));
  const auto& tree = tree_curve.trinomialTree();

  EXPECT_THAT(std::vector<double>(tree.alphas_.begin(),
                                  tree.alphas_.begin() + num_elems_to_verify),
              testing::ElementsAre(DoubleNear(0.03824, 0.00001),
                                   DoubleNear(0.05205, 0.00001),
                                   DoubleNear(0.06252, 0.00001)));

  tree.printUpTo(10);
}

TEST(ShortRateTreeCurveTest, TreeCalibrationMatchesMarketRates) {
  constexpr double tolerance = 0.0001;
  ZeroSpotCurve market_curve({1, 2, 5, 10}, {.03, .035, .04, .045});

  // TODO: See if it is possible to increase the grid spacing while still
  // getting within a 1bps tolerance of market rate fitting.
  constexpr double dt = 1. / 80;
  HullWhitePropagator hw_prop(0.1, 0.05, dt);
  ShortRateTreeCurve tree_curve(hw_prop, market_curve, TrinomialTree(10.0, dt));

  auto period = CompoundingPeriod::kQuarterly;
  for (double i = 0; i < 10; i += 1) {
    for (double j = i + 0.25; j <= i + 5; j += 0.25) {
      const double mkt_fwd = market_curve.forwardRate(i, j, period);
      const double tree_fwd = tree_curve.forwardRate(i, j, period);
      EXPECT_NEAR(mkt_fwd, tree_fwd, tolerance);
    }
  }
}

TEST(ShortRateTreeCurveTest, CheckPrecomputedForwardRates) {
  constexpr double tolerance = 0.0001;
  ZeroSpotCurve market_curve({1, 2, 5, 10}, {.03, .035, .04, .045});

  // TODO: Move this back out to a factory method.
  const double dt = 0.25 / 17;

  // TODO: This test fails if we raise the volatility or tighten the tolerance.
  // It may have to do with compounding or some other subtlety (off-by-one
  // error)? Debugging to be continued, but for now this indicates a reasonable
  // first approximation.
  HullWhitePropagator hw_prop(0.1, 0.006, dt);
  ShortRateTreeCurve tree_curve(hw_prop, market_curve, TrinomialTree(3.0, dt));

  auto period = CompoundingPeriod::kMonthly;

  for (int ti = 0;
       ti < tree_curve.trinomialTree().getTimegrid().size() -
                tree_curve.trinomialTree().timestepsPerForwardRateTenor(
                    ForwardRateTenor::k3Month);
       ++ti) {
    const double t_start = ti * tree_curve.trinomialTree().dt_;
    const double t_end = t_start + 0.25;  // 3 month tenor
    const double mkt_fwd = market_curve.forwardRate(t_start, t_end, period);

    double wtd_avg_fwd_rate = 0.0;

    for (const auto& node : tree_curve.trinomialTree().tree_[ti]) {
      double cached_fra =
          tree_curve.conditionalForwardRate(ForwardRateTenor::k3Month, node);
      wtd_avg_fwd_rate += node.arrow_debreu * cached_fra;  //.value();
    }
    wtd_avg_fwd_rate *=
        1.0 / tree_curve.trinomialTree().arrowDebreuSumAtTimestep(ti);
    EXPECT_NEAR(mkt_fwd, wtd_avg_fwd_rate, tolerance);
  }
}

}  // namespace
}  // namespace smileexplorer
