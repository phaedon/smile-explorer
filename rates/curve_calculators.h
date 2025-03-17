#ifndef SMILEEXPLORER_RATES_CURVE_CALCULATORS_H_
#define SMILEEXPLORER_RATES_CURVE_CALCULATORS_H_

#include <cmath>

#include "time/time_enums.h"

namespace smileexplorer {

inline double dfByPeriod(double r, double dt, CompoundingPeriod period) {
  const int p = static_cast<int>(period);
  switch (period) {
    case CompoundingPeriod::kAnnual:
    case CompoundingPeriod::kSemi:
    case CompoundingPeriod::kQuarterly:
    case CompoundingPeriod::kMonthly:
      return 1. / std::pow(1 + r / p, dt * p);
    case CompoundingPeriod::kContinuous:
    default:
      return std::exp(-r * dt);
  }
}

inline double fwdRateByPeriod(double df_start,
                              double df_end,
                              double dt,
                              CompoundingPeriod period) {
  if (dt == 0) {
    // TODO log an error
    return 0.;
  }
  double df_ratio = df_start / df_end;
  const int p = static_cast<int>(period);
  switch (period) {
    case CompoundingPeriod::kAnnual:
    case CompoundingPeriod::kSemi:
    case CompoundingPeriod::kQuarterly:
    case CompoundingPeriod::kMonthly:
      return p * (std::pow(df_ratio, 1. / (p * dt)) - 1);
    case CompoundingPeriod::kContinuous:
    default:
      return std::log(df_ratio) / dt;
  }
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_RATES_RATES_CURVE_H_