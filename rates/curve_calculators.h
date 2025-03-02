#ifndef MARKETS_RATES_CURVE_CALCULATORS_H_
#define MARKETS_RATES_CURVE_CALCULATORS_H_

#include "markets/time.h"

namespace markets {

inline double dfByPeriod(double r, double dt, CompoundingPeriod period) {
  switch (period) {
    case CompoundingPeriod::kContinuous:
      return std::exp(-r * dt);
    case CompoundingPeriod::kAnnual:
      return 1. / std::pow(1 + r, dt);
    case CompoundingPeriod::kSemi:
      return 1. / std::pow(1 + 0.5 * r, dt * 2);
    case CompoundingPeriod::kQuarterly:
      return 1. / std::pow(1 + 0.25 * r, dt * 4);
    case CompoundingPeriod::kMonthly:
      return 1. / std::pow(1 + (1. / 12) * r, dt * 12);
  }
}

inline double fwdRateByPeriod(double df_start,
                              double df_end,
                              double dt,
                              CompoundingPeriod period) {
  double df_ratio = df_start / df_end;
  switch (period) {
    case CompoundingPeriod::kContinuous:
      return std::log(df_ratio) / dt;
    case CompoundingPeriod::kAnnual:
      return std::pow(df_ratio, 1. / dt) - 1;
    case CompoundingPeriod::kSemi:
      return 2 * (std::pow(df_ratio, 1. / (2 * dt)) - 1);
    case CompoundingPeriod::kQuarterly:
      return 4 * (std::pow(df_ratio, 1. / (4 * dt)) - 1);
    case CompoundingPeriod::kMonthly:
      return 12 * (std::pow(df_ratio, 1. / (12 * dt)) - 1);
  }
}

}  // namespace markets

#endif  // MARKETS_RATES_RATES_CURVE_H_