
#ifndef MARKETS_STOCHASTIC_TREE_MODEL_H_
#define MARKETS_STOCHASTIC_TREE_MODEL_H_

#include "binomial_tree.h"

namespace markets {

// A tree-based representation of a stochastic process that models the diffusion
// of an underlying asset (such as a stock or commodity) or a short rate (in the
// case of interest-rate derivatives).
template <typename PropagatorT>
class StochasticTreeModel {
 public:
  StochasticTreeModel(BinomialTree binomial_tree, PropagatorT propagator)
      : binomial_tree_(binomial_tree), propagator_(propagator) {}

  template <typename VolatilityT>
  void forwardPropagate(const VolatilityT& volatility) {
    binomial_tree_.resizeWithTimeDependentVol(volatility);
    for (int t = 0; t < binomial_tree_.numTimesteps(); t++) {
      for (int i = 0; i <= t; ++i) {
        binomial_tree_.setValue(
            t, i, propagator_(binomial_tree_, volatility, t, i));
      }
    }
  }

  void forwardPropagate(const PropagatorT& fwd_prop) {
    for (int t = 0; t < binomial_tree_.numTimesteps(); t++) {
      for (int i = 0; i <= t; ++i) {
        binomial_tree_.setValue(t, i, propagator_(binomial_tree_, t, i));
      }
    }
  }

  const BinomialTree& binomialTree() const { return binomial_tree_; }

 private:
  BinomialTree binomial_tree_;
  PropagatorT propagator_;
};

}  // namespace markets

#endif  // MARKETS_BINOMIAL_TREE_H_