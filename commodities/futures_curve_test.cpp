#include "commodities/futures_curve.h"

#include <gtest/gtest.h>

#include <cmath>

namespace smileexplorer {
namespace {

TEST(FuturesCurveTest, StartsAtSpotAndHitsEveryPillar) {
  const FuturesCurve curve(3.0, {0.25, 0.5, 1.0}, {3.6, 3.1, 3.3});
  EXPECT_DOUBLE_EQ(curve.price(0.0), 3.0);
  EXPECT_DOUBLE_EQ(curve.price(-1.0), 3.0);
  EXPECT_DOUBLE_EQ(curve.price(0.25), 3.6);
  EXPECT_DOUBLE_EQ(curve.price(0.5), 3.1);
  EXPECT_DOUBLE_EQ(curve.price(1.0), 3.3);
  EXPECT_EQ(curve.times().size(), 3u);
  EXPECT_DOUBLE_EQ(curve.prices()[1], 3.1);
  EXPECT_DOUBLE_EQ(curve.spot(), 3.0);
}

TEST(FuturesCurveTest, InterpolatesInTheLogOfThePrice) {
  const FuturesCurve curve(3.0, {0.25, 0.5}, {3.6, 3.1});
  // Halfway between two pillars is their geometric mean.
  EXPECT_NEAR(curve.price(0.375), std::sqrt(3.6 * 3.1), 1e-12);
  // And before the first pillar, the curve starts from spot.
  EXPECT_NEAR(curve.price(0.125), std::sqrt(3.0 * 3.6), 1e-12);
}

TEST(FuturesCurveTest, IsFlatAfterTheLastPillar) {
  const FuturesCurve curve(3.0, {0.25, 0.5}, {3.6, 3.1});
  EXPECT_DOUBLE_EQ(curve.price(0.75), 3.1);
  EXPECT_DOUBLE_EQ(curve.price(10.0), 3.1);
}

TEST(FuturesCurveTest, WithoutPillarsIsFlatAtSpot) {
  const FuturesCurve curve(3.0, {}, {});
  EXPECT_DOUBLE_EQ(curve.price(1.0), 3.0);
}

TEST(FuturesCurveTest, ImpliedCarryRecoversTheCostOfCarry) {
  // F = S exp((r + u - y) t) with r = 4%, u = 3% and y = 1%.
  const double carry = 0.04 + 0.03 - 0.01;
  const FuturesCurve curve(
      3.0,
      {0.5, 1.0},
      {3.0 * std::exp(carry * 0.5), 3.0 * std::exp(carry * 1.0)});
  EXPECT_NEAR(curve.impliedCarry(0.5), carry, 1e-12);
  EXPECT_NEAR(curve.impliedCarry(0.75), carry, 1e-12);
  EXPECT_NEAR(curve.impliedCarry(1.0), carry, 1e-12);
}

TEST(FuturesCurveTest, ImpliedCarryIsNegativeInBackwardation) {
  const FuturesCurve curve(3.0, {0.5}, {2.7});
  EXPECT_LT(curve.impliedCarry(0.5), 0.0);
}

}  // namespace
}  // namespace smileexplorer
