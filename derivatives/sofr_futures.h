#ifndef SMILEEXPLORER_DERIVATIVES_SOFR_FUTURES_H_
#define SMILEEXPLORER_DERIVATIVES_SOFR_FUTURES_H_

#include <cmath>

#include "derivatives/hull_white_analytic.h"
#include "rates/rates_curve.h"

// Closed-form futures rates in the Hull-White model, fitted to today's
// overnight (OIS) curve, and the convexity adjustment that separates them from
// forward rates.
//
// The model: r(t) = x(t) + phi(t), where dx = -a x dt + sigma dW under the
// risk-neutral measure Q (whose numeraire is the bank account accruing at r)
// and x(0) = 0. Fitting to the curve makes
//   phi(t) = f(0, t) + sigma^2 / (2 a^2) (1 - exp(-a t))^2.
// Given x(t), the integral of r over any later interval [u, T] is Gaussian, so
// both discount factors E[exp(-int r)] and compounded accruals E[exp(+int r)]
// have closed forms. The first is a bond price; the second is what a futures
// contract on a compounded overnight rate settles on.
//
// A futures price is the Q expectation of its final settlement, because its
// daily variation margin is settled outright: no interest is paid on it.
// A collateralized forward (an FRA, cleared or under a CSA) pays interest on
// its margin at the overnight rate, so it is priced by discounting at that
// rate instead: its fair rate is the expectation under the T-forward measure.
// The difference between the two expectations is the convexity adjustment.

namespace smileexplorer {

// The mean and variance of the integral of r over [u, T], given x at time t,
// for t <= u <= T.
struct GaussianMoments {
  double mean = 0.0;
  double variance = 0.0;
};

namespace hull_white_internal {

// (1 - exp(-k h)) / k, accurate for small k h.
inline double decay(double k, double h) { return -std::expm1(-k * h) / k; }

// The integral over [u, T] of (1 - exp(-a s))^2.
inline double phiCorrectionIntegral(double a, double u, double maturity) {
  const double h = maturity - u;
  return h - 2 * std::exp(-a * u) * decay(a, h) +
         std::exp(-2 * a * u) * decay(2 * a, h);
}

// The integral over [0, h] of B(s)^2, where B(s) = (1 - exp(-a s)) / a.
inline double squaredDecayIntegral(double a, double h) {
  return (h - 2 * decay(a, h) + decay(2 * a, h)) / (a * a);
}

}  // namespace hull_white_internal

inline GaussianMoments hullWhiteIntegralMoments(const RatesCurve& curve,
                                                double mean_reversion,
                                                double sigma,
                                                double t,
                                                double x,
                                                double u,
                                                double maturity) {
  using namespace hull_white_internal;
  const double a = mean_reversion;
  const double h = maturity - u;
  const double b = decay(a, h);
  GaussianMoments m;
  // The integral of f(0, s) over [u, T] is ln(P(0, u) / P(0, T)), exactly.
  m.mean = std::log(curve.df(u) / curve.df(maturity)) +
           sigma * sigma / (2 * a * a) * phiCorrectionIntegral(a, u, maturity) +
           x * std::exp(-a * (u - t)) * b;
  // Variance from the shocks between t and u (which move x(u)), and from
  // those during [u, T].
  m.variance = sigma * sigma *
               (b * b * decay(2 * a, u - t) + squaredDecayIntegral(a, h));
  return m;
}

// The short rate at t when the model's state is x.
inline double hullWhiteShortRate(const RatesCurve& curve,
                                 double mean_reversion,
                                 double sigma,
                                 double t,
                                 double x) {
  const double a = mean_reversion;
  const double g = -std::expm1(-a * t) / a;
  return x + instantaneousForward(curve, t) + sigma * sigma / 2 * g * g;
}

// The state x at t whose short rate is r: the inverse of hullWhiteShortRate.
inline double hullWhiteState(const RatesCurve& curve,
                             double mean_reversion,
                             double sigma,
                             double t,
                             double short_rate) {
  return short_rate - hullWhiteShortRate(curve, mean_reversion, sigma, t, 0.0);
}

// The price at t of a zero-coupon bond paying 1 at T, given the state x at t.
// (The same as hullWhiteBondPrice, but in terms of x rather than r.)
inline double hullWhiteDiscountFactor(const RatesCurve& curve,
                                      double mean_reversion,
                                      double sigma,
                                      double t,
                                      double x,
                                      double maturity) {
  const GaussianMoments m =
      hullWhiteIntegralMoments(curve, mean_reversion, sigma, t, x, t, maturity);
  return std::exp(-m.mean + m.variance / 2);
}

// Today's forward rate for the period [start, end], simply compounded: the
// fair rate on a collateralized FRA (or one-period OIS) that pays the
// compounded overnight rate over the period at its end. It depends only on
// the curve.
inline double overnightForwardRate(const RatesCurve& curve,
                                   double start,
                                   double end) {
  return (curve.df(start) / curve.df(end) - 1) / (end - start);
}

// The futures rate at t (100 minus the futures price, as a decimal) of a
// contract settling on the overnight rate compounded over [start, end], such
// as CME's 3-month SOFR future, given the state x at t. Once the period has
// begun (t > start), `accrued` is the integral of r over [start, t] (the log
// of the compounding so far); before it, `accrued` is ignored.
//
// Settlement is (exp(int_start^end r) - 1) / (end - start), compounding
// continuously as an approximation of daily compounding.
inline double sofrFuturesRate(const RatesCurve& curve,
                              double mean_reversion,
                              double sigma,
                              double start,
                              double end,
                              double t = 0.0,
                              double x = 0.0,
                              double accrued = 0.0) {
  const double tau = end - start;
  if (t >= end) {
    return std::expm1(accrued) / tau;
  }
  const double u = std::max(t, start);
  const GaussianMoments m =
      hullWhiteIntegralMoments(curve, mean_reversion, sigma, t, x, u, end);
  const double already = t > start ? accrued : 0.0;
  return std::expm1(already + m.mean + m.variance / 2) / tau;
}

// The futures rate at t (t <= start) of a contract settling on a
// forward-looking term rate fixed at `start` for [start, end], simply
// compounded: 1 / P(start, end) - 1 over the period. This is the classic
// Eurodollar (and today's Euribor) futures design, ignoring any basis between
// the term rate and the curve.
inline double termRateFuturesRate(const RatesCurve& curve,
                                  double mean_reversion,
                                  double sigma,
                                  double start,
                                  double end,
                                  double t = 0.0,
                                  double x = 0.0) {
  using namespace hull_white_internal;
  const double a = mean_reversion;
  const double tau = end - start;
  // ln(1 / P(start, end)) = m0 - v0 / 2 + B x(start), with m0 and v0 the
  // moments at x(start) = 0; and x(start) given x(t) is Gaussian.
  const GaussianMoments m0 =
      hullWhiteIntegralMoments(curve, a, sigma, start, 0.0, start, end);
  const double b = decay(a, tau);
  const double x_mean = x * std::exp(-a * (start - t));
  const double x_variance = sigma * sigma * decay(2 * a, start - t);
  return std::expm1(m0.mean - m0.variance / 2 + b * x_mean +
                    b * b * x_variance / 2) /
         tau;
}

// Today's convexity adjustment for a compounded overnight futures contract:
// its futures rate less the forward rate for the same period.
inline double sofrConvexityAdjustment(const RatesCurve& curve,
                                      double mean_reversion,
                                      double sigma,
                                      double start,
                                      double end) {
  return sofrFuturesRate(curve, mean_reversion, sigma, start, end) -
         overnightForwardRate(curve, start, end);
}

inline double termRateConvexityAdjustment(const RatesCurve& curve,
                                          double mean_reversion,
                                          double sigma,
                                          double start,
                                          double end) {
  return termRateFuturesRate(curve, mean_reversion, sigma, start, end) -
         overnightForwardRate(curve, start, end);
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_DERIVATIVES_SOFR_FUTURES_H_
