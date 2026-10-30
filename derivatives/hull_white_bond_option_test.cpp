#include "derivatives/hull_white_bond_option.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "rates/zero_curve.h"

namespace smileexplorer {
namespace {

// Hull, "Options, Futures...", example 32.1 (as in
// InterestRateDerivativeTest.EuropeanBondOption): a 3-year put, struck at 63,
// on a 9-year zero-coupon bond with face 100.
ZeroSpotCurve hullCurve() {
  std::vector<double> maturities{3,
                                 31,
                                 62,
                                 94,
                                 185,
                                 367,
                                 731,
                                 1096,
                                 1461,
                                 1826,
                                 2194,
                                 2558,
                                 2922,
                                 3287,
                                 3653};
  for (auto& m : maturities) m /= 365.;
  return ZeroSpotCurve(maturities,
                       {.0501772,
                        .0498284,
                        .0497234,
                        .0496157,
                        .0499058,
                        .0509389,
                        .0579733,
                        .0630595,
                        .0673464,
                        .0694816,
                        .0708807,
                        .0727527,
                        .0730852,
                        .073979,
                        .0749015},
                       CompoundingPeriod::kContinuous,
                       CurveInterpolationStyle::kMonotonePiecewiseCubicZeros);
}

TEST(HullWhiteBondOptionTest, MatchesHull) {
  const ZeroSpotCurve curve = hullCurve();
  EXPECT_NEAR(1.8093,
              hullWhiteZeroBondOption(curve, 0.1, 0.01, 3, 9, 100, 63, false),
              0.002);
}

TEST(HullWhiteBondOptionTest, PutCallParity) {
  const ZeroSpotCurve curve = hullCurve();
  const double call =
      hullWhiteZeroBondOption(curve, 0.1, 0.01, 3, 9, 100, 63, true);
  const double put =
      hullWhiteZeroBondOption(curve, 0.1, 0.01, 3, 9, 100, 63, false);
  EXPECT_NEAR(call - put, 100 * curve.df(9) - 63 * curve.df(3), 1e-12);
}

TEST(HullWhiteBondOptionTest, IncreasesWithVolatility) {
  const ZeroSpotCurve curve = hullCurve();
  double previous = 0;
  for (double sigma : {0.005, 0.01, 0.02}) {
    const double put =
        hullWhiteZeroBondOption(curve, 0.1, sigma, 3, 9, 100, 63, false);
    EXPECT_GT(put, previous);
    previous = put;
  }
}

}  // namespace
}  // namespace smileexplorer
