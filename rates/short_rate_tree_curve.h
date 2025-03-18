
#ifndef SMILEEXPLORER_RATES_SHORT_RATE_TREE_CURVE_H_
#define SMILEEXPLORER_RATES_SHORT_RATE_TREE_CURVE_H_

#include "absl/log/log.h"
#include "curve_calculators.h"
#include "derivatives/forward_rate_agreement.h"
#include "rates_curve.h"
#include "time/time_enums.h"
#include "trees/hull_white_propagator.h"
#include "trees/trinomial_tree.h"
#include "zero_curve.h"

namespace smileexplorer {

// A wrapper for a tree representing a short-rate stochastic process, which
// provides implementations for extracting discount factors and forward rates.
// Assumes the tree has already been properly initialised and calibrated to
// match market rates.
class ShortRateTreeCurve : public RatesCurve {
 public:
  ShortRateTreeCurve(TrinomialTree trinomial_tree)
      : trinomial_tree_(std::move(trinomial_tree)) {}

  ShortRateTreeCurve(const HullWhitePropagator& propagator,
                     const ZeroSpotCurve& market_curve,
                     TrinomialTree trinomial_tree)
      : trinomial_tree_(std::move(trinomial_tree)) {
    forwardPropagate(propagator, market_curve);
  }

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

  // A single iteration of backward induction to precompute conditional forward
  // rates at a single timeslice.
  void precomputeForwardRatesForTenorAtTime(ForwardRateTenor tenor,
                                            int ti_fwd) {
    const int timesteps_in_tenor =
        trinomial_tree_.timestepsPerForwardRateTenor(tenor);

    for (int ti = ti_fwd + timesteps_in_tenor; ti >= ti_fwd; --ti) {
      for (int j = 0; j < std::ssize(trinomial_tree_.tree_[ti]); ++j) {
        auto& curr_node = trinomial_tree_.tree_[ti][j];
        curr_node.auxiliary_value = ForwardRateAgreement{}(
            trinomial_tree_, ti, j, ti_fwd + timesteps_in_tenor);
      }
    }
  }

  void precomputeForwardRatesForTenors(
      const std::vector<ForwardRateTenor> tenors) {
    for (const auto tenor : tenors) {
      const int timesteps_in_tenor =
          trinomial_tree_.timestepsPerForwardRateTenor(tenor);

      // The outermost loop computes the forward rates starting at time ti_fwd.
      for (int ti_fwd = 0;
           ti_fwd < trinomial_tree_.getTimegrid().size() - timesteps_in_tenor;
           ++ti_fwd) {
        // LOG(INFO) << "Precomputing fwd rates at ti_fwd:" << ti_fwd;
        precomputeForwardRatesForTenorAtTime(tenor, ti_fwd);

        // Cache the conditional forwards just computed.
        for (auto& node : trinomial_tree_.tree_[ti_fwd]) {
          node.forward_rate_cache.cache[tenor] =
              fwdRateByPeriod(1.0,
                              node.auxiliary_value,
                              timesteps_in_tenor * trinomial_tree_.dt_,
                              CompoundingPeriod::kMonthly);
          // LOG(INFO) << "  Cache at state j: "
          //           << node.forward_rate_cache(tenor).value()
          //           << "  and inst. fwd:"
          //           << node.state_value + trinomial_tree_.alphas_[ti_fwd];
        }
      }
    }
  }

  void forwardPropagate(const HullWhitePropagator& propagator,
                        const ZeroSpotCurve& market_curve) {
    firstStage(propagator);
    secondStage(propagator, market_curve);
  }

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

  void firstStage(const HullWhitePropagator& propagator) {
    for (size_t ti = 0; ti < trinomial_tree_.tree_.size(); ++ti) {
      LOG(INFO) << "ti: " << ti
                << " numstates:" << propagator.numStatesAtTimeIndex(ti);
      for (int state_index = 0;
           state_index < propagator.numStatesAtTimeIndex(ti);
           ++state_index) {
        trinomial_tree_.tree_[ti].push_back(propagator(ti, state_index));
      }
    }
  }

  void secondStage(const HullWhitePropagator& propagator,
                   const ZeroSpotCurve& market_curve) {
    const double dt = propagator.dt();

    trinomial_tree_.alphas_[0] = market_curve.forwardRate(0, dt);
    trinomial_tree_.tree_[0][0].arrow_debreu = 1.;

    for (size_t ti = 0; ti < trinomial_tree_.tree_.size() - 1; ++ti) {
      // Iterate over each node in the current timeslice once...
      for (int j = 0; j < std::ssize(trinomial_tree_.tree_[ti]); ++j) {
        auto& curr_node = trinomial_tree_.tree_[ti][j];
        // ... and for each iteration, update the 3 successor nodes in the next
        // timestep.
        //
        // Note that the formula in John Hull, "Options..." (pg 745,
        // formula 32.12) writes this a bit differently, as a single expression
        // for Q_{m+1,j}, as a sum which iterates over all Q_{m} which can lead
        // to this node. Mathematically that is more concise, but for the
        // implementation it would be more complicated because the number of
        // incoming edges can vary greatly, whereas the number of outgoing edges
        // is always 3.
        trinomial_tree_.updateSuccessorNodes(
            curr_node, ti, j, trinomial_tree_.alphas_[ti], dt);
      }

      trinomial_tree_.alphas_[ti + 1] =
          std::log(trinomial_tree_.arrowDebreuSumAtTimestep(ti + 1) /
                   market_curve.df(dt * (ti + 2))) /
          dt;
    }
  }
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_RATES_SHORT_RATE_TREE_CURVE_H_