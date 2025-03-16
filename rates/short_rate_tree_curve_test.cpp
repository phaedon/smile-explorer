
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

}  // namespace
}  // namespace smileexplorer
