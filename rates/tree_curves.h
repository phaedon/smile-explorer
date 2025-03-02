
#ifndef MARKETS_RATES_TREE_CURVES_H_
#define MARKETS_RATES_TREE_CURVES_H_

#include "markets/binomial_tree.h"
#include "markets/propagators.h"
#include "markets/rates/arrow_debreu.h"
#include "markets/time.h"

namespace markets {

class SimpleUncalibratedShortRatesCurve {
 public:
  SimpleUncalibratedShortRatesCurve(double time_span_years, double timestep)
      : rate_tree_(time_span_years, timestep) {
    // Default hard-coded constants for initialisation.
    updateCurve(0.05, 0.10);
  }

  void updateCurve(double spot_rate, double vol) {
    FlatVol flat_vol(vol);
    Volatility rate_vol(flat_vol);
    CRRPropagator crr_prop(spot_rate);
    rate_tree_.forwardPropagate(crr_prop, rate_vol);

    ArrowDebreauPropagator arrowdeb_prop(rate_tree_, rate_tree_.numTimesteps());
    arrowdeb_tree_ = BinomialTree::createFrom(rate_tree_);
    arrowdeb_tree_.forwardPropagate(arrowdeb_prop);

    timegrid_ = rate_tree_.getTimegrid();
  }

  double getForwardRate(double start_time, double end_time) const {
    // TODO Add some error handling for cases like end_time <= start_time, etc.
    double df_start = df(start_time);
    double df_end = df(end_time);
    double dt = end_time - start_time;
    return fwdRateByPeriod(
        df_start, df_end, dt, CompoundingPeriod::kContinuous);
  }

  double df(double time) const {
    // TODO add error handling
    int ti = timegrid_.getTimeIndexForExpiry(time).value();

    if (timegrid_.time(ti) == time) {
      return arrowdeb_tree_.sumAtTimestep(ti);
    }

    // Get the timestamp indices surrounding the requested time.
    int ti_left = ti - 1;
    int ti_right = ti;
    if (timegrid_.time(ti) > time) {
      ++ti_left;
      ++ti_right;
    }

    double fwdrate = getForwardRateByIndices(ti_left, ti_right);
    double dt = time - timegrid_.time(ti_left);
    return arrowdeb_tree_.sumAtTimestep(ti_left) *
           dfByPeriod(fwdrate, dt, CompoundingPeriod::kContinuous);
  }

 private:
  BinomialTree rate_tree_;
  BinomialTree arrowdeb_tree_;
  Timegrid timegrid_;

  double getForwardRateByIndices(int start_ti, int end_ti) const {
    double df_start = arrowdeb_tree_.sumAtTimestep(start_ti);
    double df_end = arrowdeb_tree_.sumAtTimestep(end_ti);
    double dt = timegrid_.time(end_ti) - timegrid_.time(start_ti);
    return fwdRateByPeriod(
        df_start, df_end, dt, CompoundingPeriod::kContinuous);
  }
};

}  // namespace markets

#endif  // MARKETS_RATES_TREE_CURVES_H_