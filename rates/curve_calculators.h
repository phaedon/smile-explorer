#ifndef MARKETS_RATES_CURVE_CALCULATORS_H_
#define MARKETS_RATES_CURVE_CALCULATORS_H_

#include <cmath>

#include "time/time.h"

namespace markets {

inline double dfByPeriod(double r, double dt, CompoundingPeriod period) {
  const int p = static_cast<int>(period);
  switch (period) {
    case CompoundingPeriod::kContinuous:
      return std::exp(-r * dt);
    case CompoundingPeriod::kAnnual:
    case CompoundingPeriod::kSemi:
    case CompoundingPeriod::kQuarterly:
    case CompoundingPeriod::kMonthly:
      return 1. / std::pow(1 + r / p, dt * p);
  }
}

inline double fwdRateByPeriod(double df_start,
                              double df_end,
                              double dt,
                              CompoundingPeriod period) {
  double df_ratio = df_start / df_end;
  const int p = static_cast<int>(period);
  switch (period) {
    case CompoundingPeriod::kContinuous:
      return std::log(df_ratio) / dt;
    case CompoundingPeriod::kAnnual:
    case CompoundingPeriod::kSemi:
    case CompoundingPeriod::kQuarterly:
    case CompoundingPeriod::kMonthly:
      return p * (std::pow(df_ratio, 1. / (p * dt)) - 1);
  }
}

}  // namespace markets

#endif  // MARKETS_RATES_RATES_CURVE_H_