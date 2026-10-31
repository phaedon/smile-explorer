#ifndef SMILEEXPLORER_DERIVATIVES_HULL_WHITE_ANALYTIC_H_
#define SMILEEXPLORER_DERIVATIVES_HULL_WHITE_ANALYTIC_H_

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "derivatives/hull_white_bond_option.h"
#include "rates/rates_curve.h"

// Closed-form prices in the Hull-White model, fitted to today's discount
// curve: zero-coupon bond prices at a future time given the short rate then,
// and from them caplets and (by Jamshidian's decomposition) options on coupon
// bonds and swaptions. See Hull, "Options, Futures, and Other Derivatives",
// chapter 32.

namespace smileexplorer {

// The instantaneous forward rate f(0, t) implied by the curve, by central
// difference of -ln P(0, t).
inline double instantaneousForward(const RatesCurve& curve, double t) {
  constexpr double h = 1e-5;
  const double lo = std::max(0.0, t - h);
  const double hi = t + h;
  return -(std::log(curve.df(hi)) - std::log(curve.df(lo))) / (hi - lo);
}

// The price at time t of a zero-coupon bond paying 1 at time T, when the short
// rate at t is r: P(t, T) = A(t, T) exp(-B(t, T) r), where
//   B(t, T) = (1 - exp(-a (T - t))) / a
//   ln A(t, T) = ln(P(0, T) / P(0, t)) + B f(0, t)
//                - sigma^2 / (4 a) (1 - exp(-2 a t)) B^2.
inline double hullWhiteBondPrice(const RatesCurve& curve,
                                 double mean_reversion,
                                 double sigma,
                                 double t,
                                 double maturity,
                                 double short_rate) {
  const double a = mean_reversion;
  const double b = (1 - std::exp(-a * (maturity - t))) / a;
  const double log_a =
      std::log(curve.df(maturity) / curve.df(t)) +
      b * instantaneousForward(curve, t) -
      sigma * sigma / (4 * a) * (1 - std::exp(-2 * a * t)) * b * b;
  return std::exp(log_a - b * short_rate);
}

// A European option, expiring at `expiry`, to buy (call) or sell (put) for
// `strike` a bond paying amounts[i] at times[i] (all after expiry).
//
// Jamshidian: every bond price falls as the short rate at expiry rises, so
// the option is exercised exactly when that rate is on one side of the r*
// at which the bond is worth the strike. The option is then a portfolio of
// options on each zero-coupon bond, struck at its own value when r = r*.
inline double hullWhiteCouponBondOption(const RatesCurve& curve,
                                        double mean_reversion,
                                        double sigma,
                                        double expiry,
                                        const std::vector<double>& times,
                                        const std::vector<double>& amounts,
                                        double strike,
                                        bool is_call) {
  // Each bond price is A_i exp(-B_i r): compute A_i and B_i once.
  const double a = mean_reversion;
  std::vector<double> coef(times.size());
  std::vector<double> b(times.size());
  for (size_t i = 0; i < times.size(); ++i) {
    b[i] = (1 - std::exp(-a * (times[i] - expiry))) / a;
    coef[i] =
        amounts[i] * hullWhiteBondPrice(curve, a, sigma, expiry, times[i], 0.0);
  }
  // The bond's value, less the strike, and its derivative in r.
  const auto excess = [&](double r) {
    double value = -strike;
    double slope = 0.0;
    for (size_t i = 0; i < times.size(); ++i) {
      const double v = coef[i] * std::exp(-b[i] * r);
      value += v;
      slope -= b[i] * v;
    }
    return std::pair{value, slope};
  };
  // The value falls as r rises, and is convex in r, so Newton's method from
  // the left of the root converges monotonically; bisection is the fallback.
  double lo = -1.0;
  double hi = 1.0;
  while (excess(lo).first < 0 && lo > -100) lo *= 2;
  while (excess(hi).first > 0 && hi < 100) hi *= 2;
  double r_star = lo;
  for (int k = 0; k < 100; ++k) {
    const auto [value, slope] = excess(r_star);
    if (std::abs(value) < 1e-14 * std::max(1.0, strike)) break;
    (value > 0 ? lo : hi) = r_star;
    double next = r_star - value / slope;
    if (!(next > lo && next < hi)) next = 0.5 * (lo + hi);
    r_star = next;
  }

  double price = 0.0;
  for (size_t i = 0; i < times.size(); ++i) {
    const double strike_i =
        amounts[i] *
        hullWhiteBondPrice(
            curve, mean_reversion, sigma, expiry, times[i], r_star);
    price += hullWhiteZeroBondOption(curve,
                                     mean_reversion,
                                     sigma,
                                     expiry,
                                     times[i],
                                     amounts[i],
                                     strike_i,
                                     is_call);
  }
  return price;
}

// A European swaption, expiring at `expiry`, into a swap that starts then,
// exchanging fixed `fixed_rate` (paid at `payment_times`, each accruing since
// the previous one) for floating, on `notional`. A payer swaption (pay fixed)
// is a put, struck at par, on the bond paying the fixed coupons and the
// notional; a receiver swaption is the call.
inline double hullWhiteSwaption(const RatesCurve& curve,
                                double mean_reversion,
                                double sigma,
                                double expiry,
                                const std::vector<double>& payment_times,
                                double fixed_rate,
                                double notional,
                                bool is_payer) {
  std::vector<double> amounts;
  double previous = expiry;
  for (double t : payment_times) {
    amounts.push_back(fixed_rate * (t - previous) * notional);
    previous = t;
  }
  if (!amounts.empty()) amounts.back() += notional;
  return hullWhiteCouponBondOption(curve,
                                   mean_reversion,
                                   sigma,
                                   expiry,
                                   payment_times,
                                   amounts,
                                   notional,
                                   !is_payer);
}

// A caplet (or floorlet) on the simple rate from `start` to `end`, struck at
// `strike`, paying notional * (end - start) * max(rate - strike, 0) at `end`.
// It is (1 + strike tau) times a put (for a floorlet, a call) on the
// zero-coupon bond from start to end, struck at 1 / (1 + strike tau).
inline double hullWhiteCaplet(const RatesCurve& curve,
                              double mean_reversion,
                              double sigma,
                              double start,
                              double end,
                              double strike,
                              double notional,
                              bool is_cap) {
  const double tau = end - start;
  return hullWhiteZeroBondOption(curve,
                                 mean_reversion,
                                 sigma,
                                 start,
                                 end,
                                 notional * (1 + strike * tau),
                                 notional,
                                 !is_cap);
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_DERIVATIVES_HULL_WHITE_ANALYTIC_H_
