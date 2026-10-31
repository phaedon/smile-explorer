#ifndef SMILEEXPLORER_RATES_SWAP_RATES_H_
#define SMILEEXPLORER_RATES_SWAP_RATES_H_

#include <vector>

#include "rates_curve.h"

// Today's forward rates, annuities and par swap rates, from a discount curve.

namespace smileexplorer {

// The simple (money-market) forward rate from `start` to `end`.
inline double simpleForwardRate(const RatesCurve& curve,
                                double start,
                                double end) {
  return (curve.df(start) / curve.df(end) - 1) / (end - start);
}

// The present value of receiving 1 per year, accrued over each period from
// `start` to the payment times: sum of (t_i - t_{i-1}) P(0, t_i).
inline double annuity(const RatesCurve& curve,
                      double start,
                      const std::vector<double>& payment_times) {
  double value = 0.0;
  double previous = start;
  for (double t : payment_times) {
    value += (t - previous) * curve.df(t);
    previous = t;
  }
  return value;
}

// The fixed rate at which a swap from `start` to the last payment time is
// worth zero today: (P(0, start) - P(0, end)) / annuity.
inline double parSwapRate(const RatesCurve& curve,
                          double start,
                          const std::vector<double>& payment_times) {
  return (curve.df(start) - curve.df(payment_times.back())) /
         annuity(curve, start, payment_times);
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_RATES_SWAP_RATES_H_
