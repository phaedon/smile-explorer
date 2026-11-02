#include "derivatives/sofr_futures.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <numbers>

#include "derivatives/hull_white_analytic.h"
#include "derivatives/short_rate_rollback.h"
#include "rates/short_rate_tree_curve.h"
#include "rates/zero_curve.h"
#include "trees/hull_white_propagator.h"

namespace smileexplorer {
namespace {

ZeroSpotCurve upwardCurve() {
  return ZeroSpotCurve({0.5, 1, 2, 5, 10}, {0.03, 0.032, 0.035, 0.04, 0.043});
}

// The expectation of g(x) for x ~ N(0, variance), by the trapezoid rule.
template <typename F>
double gaussianExpectation(double variance, const F& g) {
  const double sd = std::sqrt(variance);
  constexpr int kPoints = 4001;
  const double lo = -10 * sd;
  const double h = 20 * sd / (kPoints - 1);
  double sum = 0.0;
  for (int k = 0; k < kPoints; ++k) {
    const double x = lo + k * h;
    const double w = (k == 0 || k == kPoints - 1) ? 0.5 : 1.0;
    sum += w * g(x) * std::exp(-x * x / (2 * variance));
  }
  return sum * h / (sd * std::sqrt(2 * std::numbers::pi));
}

TEST(SofrFuturesTest, DiscountFactorTodayIsTheCurve) {
  const ZeroSpotCurve curve = upwardCurve();
  for (double maturity : {0.25, 1.0, 3.5, 8.0}) {
    EXPECT_NEAR(curve.df(maturity),
                hullWhiteDiscountFactor(curve, 0.05, 0.01, 0, 0, maturity),
                1e-12);
  }
}

// The x-based bond price agrees with Hull's r-based A(t, T) exp(-B r).
TEST(SofrFuturesTest, DiscountFactorMatchesHullWhiteBondPrice) {
  const ZeroSpotCurve curve = upwardCurve();
  const double a = 0.05;
  const double sigma = 0.012;
  for (double x : {-0.02, 0.0, 0.015}) {
    const double r = hullWhiteShortRate(curve, a, sigma, 2.0, x);
    EXPECT_NEAR(hullWhiteBondPrice(curve, a, sigma, 2.0, 6.0, r),
                hullWhiteDiscountFactor(curve, a, sigma, 2.0, x, 6.0),
                1e-8);
    EXPECT_NEAR(x, hullWhiteState(curve, a, sigma, 2.0, r), 1e-14);
  }
}

TEST(SofrFuturesTest, AdjustmentVanishesWithoutVolatility) {
  const ZeroSpotCurve curve = upwardCurve();
  EXPECT_NEAR(0.0, sofrConvexityAdjustment(curve, 0.05, 1e-8, 3, 3.25), 1e-14);
  EXPECT_NEAR(
      0.0, termRateConvexityAdjustment(curve, 0.05, 1e-8, 3, 3.25), 1e-14);
}

// Futures rates sit above forward rates, by roughly sigma^2 times the square of
// the time to expiry; compounding in arrears adds the period's own variance.
TEST(SofrFuturesTest, AdjustmentIsPositiveGrowingAndQuadraticInSigma) {
  const ZeroSpotCurve curve = upwardCurve();
  const double a = 0.03;
  double previous = 0.0;
  for (double start : {0.0, 1.0, 2.0, 5.0, 9.0}) {
    const double ca =
        sofrConvexityAdjustment(curve, a, 0.01, start, start + 0.25);
    EXPECT_GT(ca, previous);
    EXPECT_GT(ca,
              termRateConvexityAdjustment(curve, a, 0.01, start, start + 0.25));
    previous = ca;
  }
  const double ca1 = sofrConvexityAdjustment(curve, a, 0.01, 5, 5.25);
  const double ca2 = sofrConvexityAdjustment(curve, a, 0.02, 5, 5.25);
  EXPECT_NEAR(4.0, ca2 / ca1, 0.05);
}

// With no mean reversion (Ho-Lee), x = sigma W. Writing tau = E - S, the
// expected compounding over [S, E] exceeds the forward's P(0, S) / P(0, E) by
// a factor exp(c), with
//   c = sigma^2 (S^2 tau / 2 + S tau^2 + tau^3 / 3).
// For the term rate, E[1 / P(S, E)] exceeds it by exp(c'), with
//   c' = sigma^2 (S^2 tau / 2 + S tau^2):
// Hull's sigma^2 S E / 2 on the continuously compounded rate, times tau, plus
// sigma^2 S tau^2 / 2 from compounding it simply. Compounding in arrears adds
// the variance of the rate during the period itself, sigma^2 tau^3 / 3.
TEST(SofrFuturesTest, HoLeeLimit) {
  const ZeroSpotCurve curve = upwardCurve();
  const double sigma = 0.01;
  const double s = 4.0;
  const double e = 4.25;
  const double tau = e - s;
  const double ratio = curve.df(s) / curve.df(e);

  const double c =
      sigma * sigma * (s * s * tau / 2 + s * tau * tau + tau * tau * tau / 3);
  EXPECT_NEAR(std::expm1(std::log(ratio) + c) / tau,
              sofrFuturesRate(curve, 1e-6, sigma, s, e),
              2e-8);

  const double c_term = sigma * sigma * (s * s * tau / 2 + s * tau * tau);
  EXPECT_NEAR(std::expm1(std::log(ratio) + c_term) / tau,
              termRateFuturesRate(curve, 1e-6, sigma, s, e),
              2e-8);
}

// A futures rate is a Q-martingale: averaging tomorrow's futures rate over the
// state's distribution then recovers today's.
TEST(SofrFuturesTest, FuturesRateIsAMartingale) {
  const ZeroSpotCurve curve = upwardCurve();
  const double a = 0.05;
  const double sigma = 0.012;
  const double t = 1.5;
  const double x_variance =
      sigma * sigma * (1 - std::exp(-2 * a * t)) / (2 * a);
  const double today = sofrFuturesRate(curve, a, sigma, 3, 3.25);
  const double expected = gaussianExpectation(x_variance, [&](double x) {
    return sofrFuturesRate(curve, a, sigma, 3, 3.25, t, x);
  });
  EXPECT_NEAR(today, expected, 1e-12);

  const double term_today = termRateFuturesRate(curve, a, sigma, 3, 3.25);
  const double term_expected = gaussianExpectation(x_variance, [&](double x) {
    return termRateFuturesRate(curve, a, sigma, 3, 3.25, t, x);
  });
  EXPECT_NEAR(term_today, term_expected, 1e-12);
}

// Inside the period, the rate combines what has accrued with what's to come,
// and at the end it is just what accrued.
TEST(SofrFuturesTest, PartlyFixedContract) {
  const ZeroSpotCurve curve = upwardCurve();
  const double a = 0.05;
  const double sigma = 0.01;
  const double accrued = 0.04 * 0.1;
  EXPECT_NEAR(std::expm1(accrued) / 0.25,
              sofrFuturesRate(curve, a, sigma, 1, 1.25, 1.25, 0.003, accrued),
              1e-15);
  // Just before the end, almost nothing is left to accrue.
  const double r = hullWhiteShortRate(curve, a, sigma, 1.25 - 1e-6, 0.003);
  EXPECT_NEAR(
      std::expm1(accrued + r * 1e-6) / 0.25,
      sofrFuturesRate(curve, a, sigma, 1, 1.25, 1.25 - 1e-6, 0.003, accrued),
      1e-10);
}

// Same tree, two rollbacks: discounting gives the curve back (so the forward
// rate), while the undiscounted expectation of the compounding gives the
// futures rate, which converges to the closed form as the steps shrink.
TEST(SofrFuturesTest, TreeAgreesWithClosedForm) {
  const ZeroSpotCurve curve = upwardCurve();
  const double a = 0.05;
  const double sigma = 0.012;
  const double dt = 1.0 / 48;
  const int ti_start = 96;  // 2 years
  const int ti_end = 108;   // 2.25 years
  ShortRateTreeCurve tree_curve(
      std::make_unique<HullWhitePropagator>(a, sigma, dt),
      curve,
      ti_end * dt * (1 - 1e-12));
  const TrinomialTree& tree = tree_curve.trinomialTree();
  ASSERT_EQ(ti_end + 1, std::ssize(tree.tree_));
  const double s = ti_start * dt;
  const double e = ti_end * dt;

  const double p_start = bondPrices(tree, ti_start)[0][0];
  const double p_end = bondPrices(tree, ti_end)[0][0];
  EXPECT_NEAR(curve.df(s), p_start, 1e-10);
  EXPECT_NEAR(curve.df(e), p_end, 1e-10);
  const double tree_forward = (p_start / p_end - 1) / (e - s);
  EXPECT_NEAR(overnightForwardRate(curve, s, e), tree_forward, 1e-8);

  const double tree_futures =
      (expectedCompounding(tree, ti_start, ti_end)[0][0] - 1) / (e - s);
  const double ca = sofrConvexityAdjustment(curve, a, sigma, s, e);
  // About 3 basis points; the tree gets it to within a twentieth of one.
  EXPECT_GT(ca, 2e-4);
  EXPECT_NEAR(ca, tree_futures - tree_forward, 5e-6);
}

}  // namespace
}  // namespace smileexplorer
