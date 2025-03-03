
#ifndef MARKETS_RATES_TREE_CURVES_H_
#define MARKETS_RATES_TREE_CURVES_H_

#include "curve_calculators.h"
#include "markets/binomial_tree.h"
#include "markets/propagators.h"
#include "markets/rates/arrow_debreu.h"
#include "markets/stochastic_tree_model.h"
#include "markets/time.h"

namespace markets {

class SimpleUncalibratedShortRatesCurve {
 public:
  SimpleUncalibratedShortRatesCurve(double time_span_years, double timestep)
      : short_rate_model_(BinomialTree(time_span_years, timestep),
                          CRRPropagator(0.05)),
        arrowdeb_model_(
            BinomialTree::createFrom(short_rate_model_.binomialTree()),
            ArrowDebreauPropagator(
                short_rate_model_.binomialTree(),
                short_rate_model_.binomialTree().numTimesteps())) {
    // Default hard-coded constants for initialisation.
    updateCurve(0.05, 0.10);
  }

  void updateCurve(double spot_rate, double vol) {
    short_rate_model_.updateSpot(spot_rate);
    Volatility rate_vol(FlatVol{vol});
    short_rate_model_.forwardPropagate(rate_vol);
    // These are already linked because the short_rate binomial tree was passed
    // to this model by reference.
    arrowdeb_model_.forwardPropagate();

    timegrid_ = short_rate_model_.binomialTree().getTimegrid();
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
      return arrowdeb_model_.binomialTree().sumAtTimestep(ti);
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
    return arrowdeb_model_.binomialTree().sumAtTimestep(ti_left) *
           dfByPeriod(fwdrate, dt, CompoundingPeriod::kContinuous);
  }

 private:
  StochasticTreeModel<CRRPropagator> short_rate_model_;
  StochasticTreeModel<ArrowDebreauPropagator> arrowdeb_model_;
  Timegrid timegrid_;

  double getForwardRateByIndices(int start_ti, int end_ti) const {
    double df_start = arrowdeb_model_.binomialTree().sumAtTimestep(start_ti);
    double df_end = arrowdeb_model_.binomialTree().sumAtTimestep(end_ti);
    double dt = timegrid_.time(end_ti) - timegrid_.time(start_ti);
    return fwdRateByPeriod(
        df_start, df_end, dt, CompoundingPeriod::kContinuous);
  }
};

}  // namespace markets

#endif  // MARKETS_RATES_TREE_CURVES_H_