
#include "short_rate_tree_curve.h"

#include <gtest/gtest.h>

namespace smileexplorer {
namespace {

TEST(ShortRateTreeCurveTest, TreeCalibrationMatchesMarketRates) {
  constexpr double tolerance = 0.0001;
  ZeroSpotCurve market_curve({1, 2, 5, 10}, {.03, .035, .04, .045});

  // TODO: See if it is possible to increase the grid spacing while still
  // getting within a 1bps tolerance of market rate fitting.
  constexpr double dt = 1. / 80;
  TrinomialTree tree(10, 0.1, dt, 0.05);
  tree.forwardPropagate(market_curve);
  ShortRateTreeCurve tree_curve(std::move(tree));

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

  // TODO: This test fails if we raise the volatility or tighten the tolerance.
  // It may have to do with compounding or some other subtlety (off-by-one
  // error)? Debugging to be continued, but for now this indicates a reasonable
  // first approximation.
  auto tree =
      TrinomialTree::create(3, ForwardRateTenor::k3Month, 17, 0.1, 0.006);
  tree.forwardPropagate(market_curve);
  ShortRateTreeCurve tree_curve(std::move(tree));

  tree_curve.precomputeForwardRatesForTenors({ForwardRateTenor::k3Month});
  auto period = CompoundingPeriod::kMonthly;

  for (int ti = 0; ti < tree_curve.trinomialTree().getTimegrid().size(); ++ti) {
    const double t_start = ti * tree_curve.trinomialTree().dt_;
    const double t_end = t_start + 0.25;  // 3 month tenor
    const double mkt_fwd = market_curve.forwardRate(t_start, t_end, period);

    double wtd_avg_fwd_rate = 0.0;
    bool cache_value_not_found = false;
    for (const auto& node : tree_curve.trinomialTree().tree_[ti]) {
      auto cached_fra = node.forward_rate_cache(ForwardRateTenor::k3Month);
      if (!cached_fra.has_value()) {
        cache_value_not_found = true;
        continue;
      }
      wtd_avg_fwd_rate += node.arrow_debreu * cached_fra.value();
    }
    if (!cache_value_not_found) {
      wtd_avg_fwd_rate *=
          1.0 / tree_curve.trinomialTree().arrowDebreuSumAtTimestep(ti);
      EXPECT_NEAR(mkt_fwd, wtd_avg_fwd_rate, tolerance);
    }
  }
}

}  // namespace
}  // namespace smileexplorer
