
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

  ShortRateTreeCurve tree_curve(
      std::make_unique<HullWhitePropagator>(.1, sigma, dt),
      market_curve,
      num_elems_to_verify * dt);
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
  ShortRateTreeCurve tree_curve(
      std::make_unique<HullWhitePropagator>(0.1, 0.05, dt), market_curve, 10.0);

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
