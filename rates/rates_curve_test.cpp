#include "rates_curve.h"

#include <gtest/gtest.h>

#include <cmath>

#include "tree_curves.h"

namespace markets {
namespace {

TEST(RatesCurveTest, SimpleUncalibrated_Basic) {
  // TODO these are all approximations / sanity-checks during development and
  // are expected to fail, for now.
  SimpleUncalibratedShortRatesCurve curve(10.0, 0.1);
  EXPECT_NEAR(0.05, curve.forwardRate(0, 1), 0.0005);
  EXPECT_NEAR(0.05, curve.forwardRate(1, 2), 0.0005);
  EXPECT_NEAR(0.05, curve.forwardRate(0, 2), 0.0005);
  EXPECT_NEAR(0.05, curve.forwardRate(5, 7), 0.0010);

  EXPECT_NEAR(0.95, curve.df(1.0), 0.01);
  EXPECT_NEAR(0.9, curve.df(2.0), 0.01);
  EXPECT_NEAR(0.895, curve.df(2.1), 0.01);

  EXPECT_NEAR(1.0 / (std::pow(1.05, 5)), curve.df(5.0), 0.01);
}

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
}  // namespace markets
