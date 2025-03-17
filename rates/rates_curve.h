#ifndef SMILEEXPLORER_RATES_RATES_CURVE_H_
#define SMILEEXPLORER_RATES_RATES_CURVE_H_

#include <utility>  // std::pair

#include "curve_calculators.h"
#include "time/time_enums.h"

namespace smileexplorer {

class RatesCurve {
 public:
  virtual ~RatesCurve() = default;

  // Return discount factor at any time in the future.
  virtual double df(double time) const = 0;

  double forwardRate(
      double start_time,
      double end_time,
      CompoundingPeriod period = CompoundingPeriod::kContinuous) const {
    if (end_time == 0) {
      // Hard-code to 1 month just to prevent division by 0.
      end_time = 1 / 12.;
    }
    double df_start = df(start_time);
    double df_end = df(end_time);
    double dt = end_time - start_time;
    return fwdRateByPeriod(df_start, df_end, dt, period);
  }

  double forwardDF(double start_time, double end_time) const {
    return df(end_time) / df(start_time);
  }

  double inverseForwardDF(double start_time, double end_time) const {
    return df(start_time) / df(end_time);
  }
};

// Only used for testing. This can be moved to the test lib (along with other
// such things, like fake example vol surfaces with hardcoded params).
class NoDiscountingCurve : public RatesCurve {
 public:
  ~NoDiscountingCurve() override = default;
  double df(double time) const override { return 1.0; }
};

// Utility for convenient extraction of two spot rates (continuously compounded)
// for two different rate curves. These are the values passed to Black-Scholes.
// We use "foreign" and "domestic" as per the FOR-DOM convention in FX.
inline std::pair<double, double> dualCurrencyRates(
    double t,
    const RatesCurve& foreign_rates,
    const RatesCurve& domestic_rates) {
  double df_dom = domestic_rates.df(t);
  double df_for = foreign_rates.df(t);
  double r_dom =
      fwdRateByPeriod(1.0, df_dom, t, CompoundingPeriod::kContinuous);
  double r_for =
      fwdRateByPeriod(1.0, df_for, t, CompoundingPeriod::kContinuous);
  return {r_for, r_dom};
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_RATES_RATES_CURVE_H_