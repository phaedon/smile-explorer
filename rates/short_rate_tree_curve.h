
#ifndef SMILEEXPLORER_RATES_SHORT_RATE_TREE_CURVE_H_
#define SMILEEXPLORER_RATES_SHORT_RATE_TREE_CURVE_H_

#include "curve_calculators.h"
#include "rates_curve.h"
#include "time.h"
#include "trees/trinomial_tree.h"

namespace smileexplorer {

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

#endif  // SMILEEXPLORER_RATES_SHORT_RATE_TREE_CURVE_H_