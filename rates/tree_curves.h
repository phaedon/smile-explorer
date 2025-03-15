
#ifndef SMILEEXPLORER_RATES_TREE_CURVES_H_
#define SMILEEXPLORER_RATES_TREE_CURVES_H_

#include "arrow_debreu.h"
#include "curve_calculators.h"
#include "rates_curve.h"
#include "time.h"
#include "trees/binomial_tree.h"
#include "trees/propagators.h"
#include "trees/stochastic_tree_model.h"
#include "trees/trinomial_tree.h"

namespace smileexplorer {

class SimpleUncalibratedShortRatesCurve : public RatesCurve {
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

  double df(double time) const override {
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

// A wrapper for a tree representing a short-rate stochastic process, which
// provides implementations for extracting discount factors and forward rates.
// Assumes the tree has already been properly initialised and calibrated to
// match market rates.
class ShortRateTreeCurve : public RatesCurve {
 public:
  ShortRateTreeCurve(TrinomialTree trinomial_tree)
      : trinomial_tree_(std::move(trinomial_tree)) {}

  double df(double time) const override {
    // TODO add error handling
    const auto& timegrid = trinomial_tree_.getTimegrid();
    auto ti_candidate = timegrid.getTimeIndexForExpiry(time);
    int ti = ti_candidate.value_or(timegrid.size() - 1);

    if (timegrid.time(ti) == time) {
      return trinomial_tree_.arrowDebreuSumAtTimestep(ti);
    }

    // Get the timestamp indices surrounding the requested time.
    int ti_left = ti;
    int ti_right = ti + 1;
    if (timegrid.time(ti) > time) {
      --ti_left;
      --ti_right;
    } else if (ti >= timegrid.size() - 1) {
      // Bug fix to extrapolate past the end of the curve.
      ti_left = timegrid.size() - 2;
      ti_right = ti_left + 1;
    }

    double fwdrate = getForwardRateByIndices(
        ti_left, ti_right, CompoundingPeriod::kContinuous);
    double dt = time - timegrid.time(ti_left);
    return trinomial_tree_.arrowDebreuSumAtTimestep(ti_left) *
           dfByPeriod(fwdrate, dt, CompoundingPeriod::kContinuous);
  }

  const TrinomialTree& trinomialTree() const { return trinomial_tree_; }
  TrinomialTree& trinomialTree() { return trinomial_tree_; }

 private:
  TrinomialTree trinomial_tree_;

  double getForwardRateByIndices(int start_ti,
                                 int end_ti,
                                 CompoundingPeriod period) const {
    const auto& timegrid = trinomial_tree_.getTimegrid();

    double df_start = trinomial_tree_.arrowDebreuSumAtTimestep(start_ti);
    double df_end = trinomial_tree_.arrowDebreuSumAtTimestep(end_ti);
    double dt = timegrid.time(end_ti) - timegrid.time(start_ti);
    return fwdRateByPeriod(df_start, df_end, dt, period);
  }
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_RATES_TREE_CURVES_H_