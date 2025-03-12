#include "rates/zero_curve.h"

#include <gtest/gtest.h>

namespace smileexplorer {
namespace {

TEST(RatesCurveTest, ZeroSpotCurve) {
  // Follows the example in Derman on pg. 486 (question 13-6)
  ZeroSpotCurve zeros(
      {1, 2, 3}, {0.05, 0.0747, 0.0992}, CompoundingPeriod::kAnnual);

  // Textbook numbers are rounded slightly; that's ok.
  EXPECT_NEAR(0.1, zeros.forwardRate(1, 2), 0.0001);
  EXPECT_NEAR(0.15, zeros.forwardRate(2, 3), 0.0002);

  // Ensure constant forwards past the end of the provided curve.
  EXPECT_NEAR(0.15, zeros.forwardRate(100, 200), 0.0002);

  EXPECT_DOUBLE_EQ(1.0, zeros.df(0.0));
  EXPECT_EQ(0, zeros.findClosestMaturityIndex(0.1));
  EXPECT_NEAR(std::exp(-0.05 * 0.1), zeros.df(0.1), 0.0005);

  for (double i = 0.0; i < 0.9; i += 0.1) {
    EXPECT_NEAR(0.05, zeros.forwardRate(i, i + 0.1), 0.0005);
  }
}

}  // namespace
}  // namespace smileexplorer
