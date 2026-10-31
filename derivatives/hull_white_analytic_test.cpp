#include "derivatives/hull_white_analytic.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "rates/zero_curve.h"

namespace smileexplorer {
namespace {

ZeroSpotCurve upwardCurve() {
  return ZeroSpotCurve({0.5, 1, 2, 5, 10}, {0.03, 0.032, 0.035, 0.04, 0.043});
}

TEST(HullWhiteAnalyticTest, BondPriceTodayIsTheCurve) {
  const ZeroSpotCurve curve = upwardCurve();
  const double r0 = instantaneousForward(curve, 0);
  for (double maturity : {0.5, 1.5, 3.0, 7.0}) {
    EXPECT_NEAR(curve.df(maturity),
                hullWhiteBondPrice(curve, 0.1, 0.01, 0, maturity, r0),
                1e-6);
  }
}

// The expected discounted bond price at t is today's forward price: averaging
// P(t, T) over the short rate's distribution at t, weighted by discounting,
// recovers P(0, T). Checked here more simply: higher short rates mean lower
// bond prices, and the sensitivity is B(t, T).
TEST(HullWhiteAnalyticTest, BondPriceFallsWithTheShortRate) {
  const ZeroSpotCurve curve = upwardCurve();
  const double a = 0.1;
  const double p1 = hullWhiteBondPrice(curve, a, 0.01, 2, 7, 0.03);
  const double p2 = hullWhiteBondPrice(curve, a, 0.01, 2, 7, 0.04);
  EXPECT_LT(p2, p1);
  EXPECT_NEAR((1 - std::exp(-a * 5)) / a, std::log(p1 / p2) / 0.01, 1e-9);
}

// Hull, "Options, Futures...", problem 32.8 (as priced on the tree in
// InterestRateDerivativeTest.CouponBondOption): a 2.1-year call, struck at
// 99, on a bond paying 2.5 at 2.5 years and 102.5 at 3 years.
TEST(HullWhiteAnalyticTest, CouponBondOptionMatchesHull) {
  const ZeroSpotCurve curve({0.25, 5},
                            {.06, .06},
                            CompoundingPeriod::kSemi,
                            CurveInterpolationStyle::kConstantForwards);
  EXPECT_NEAR(0.944596,
              hullWhiteCouponBondOption(
                  curve, 0.05, 0.015, 2.1, {2.5, 3.0}, {2.5, 102.5}, 99, true),
              0.002);
}

TEST(HullWhiteAnalyticTest, CouponBondPutCallParity) {
  const ZeroSpotCurve curve = upwardCurve();
  const std::vector<double> times{3, 4, 5};
  const std::vector<double> amounts{4, 4, 104};
  const double call =
      hullWhiteCouponBondOption(curve, 0.1, 0.01, 2, times, amounts, 100, true);
  const double put = hullWhiteCouponBondOption(
      curve, 0.1, 0.01, 2, times, amounts, 100, false);
  double bond = 0;
  for (size_t i = 0; i < times.size(); ++i) {
    bond += amounts[i] * curve.df(times[i]);
  }
  EXPECT_NEAR(call - put, bond - 100 * curve.df(2), 1e-9);
}

// A payer swaption less a receiver swaption is a forward-starting payer swap.
TEST(HullWhiteAnalyticTest, SwaptionParity) {
  const ZeroSpotCurve curve = upwardCurve();
  const std::vector<double> payments{2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6, 6.5, 7};
  const double fixed = 0.04;
  const double payer =
      hullWhiteSwaption(curve, 0.1, 0.01, 2, payments, fixed, 100, true);
  const double receiver =
      hullWhiteSwaption(curve, 0.1, 0.01, 2, payments, fixed, 100, false);
  double annuity = 0;
  for (double t : payments) annuity += 0.5 * curve.df(t);
  const double swap = 100 * (curve.df(2) - curve.df(7) - fixed * annuity);
  EXPECT_NEAR(swap, payer - receiver, 1e-9);
  EXPECT_GT(payer, 0);
  EXPECT_GT(receiver, 0);
}

// A caplet less a floorlet pays the rate less the strike: a forward rate
// agreement, worth P(0, start) - (1 + K tau) P(0, end) per unit notional.
TEST(HullWhiteAnalyticTest, CapletFloorletParity) {
  const ZeroSpotCurve curve = upwardCurve();
  const double cap =
      hullWhiteCaplet(curve, 0.1, 0.01, 3, 3.25, 0.04, 100, true);
  const double floor =
      hullWhiteCaplet(curve, 0.1, 0.01, 3, 3.25, 0.04, 100, false);
  EXPECT_NEAR(100 * (curve.df(3) - (1 + 0.04 * 0.25) * curve.df(3.25)),
              cap - floor,
              1e-9);
}

TEST(HullWhiteAnalyticTest, PricesRiseWithVolAndFallWithMeanReversion) {
  const ZeroSpotCurve curve = upwardCurve();
  const std::vector<double> payments{3, 4, 5};
  const auto swaption = [&](double a, double sigma) {
    return hullWhiteSwaption(curve, a, sigma, 2, payments, 0.04, 100, true);
  };
  const auto caplet = [&](double a, double sigma) {
    return hullWhiteCaplet(curve, a, sigma, 3, 3.25, 0.04, 100, true);
  };
  EXPECT_LT(swaption(0.1, 0.01), swaption(0.1, 0.015));
  EXPECT_GT(swaption(0.05, 0.01), swaption(0.3, 0.01));
  EXPECT_LT(caplet(0.1, 0.01), caplet(0.1, 0.015));
  EXPECT_GT(caplet(0.05, 0.01), caplet(0.3, 0.01));
}

}  // namespace
}  // namespace smileexplorer
