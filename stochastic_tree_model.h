
#ifndef MARKETS_STOCHASTIC_TREE_MODEL_H_
#define MARKETS_STOCHASTIC_TREE_MODEL_H_

#include "absl/log/log.h"
#include "binomial_tree.h"
#include "derivative.h"

namespace markets {

// A tree-based representation of a stochastic process that models the
// diffusion of an underlying asset (such as a stock or commodity) or a
// short rate (in the case of interest-rate derivatives).
template <typename PropagatorT>
class StochasticTreeModel {
 public:
  StochasticTreeModel(BinomialTree binomial_tree, PropagatorT propagator)
      : binomial_tree_(binomial_tree), propagator_(propagator) {}

  template <typename VolatilityT>
  void forwardPropagate(const VolatilityT& volatility) {
    binomial_tree_.resizeWithTimeDependentVol(volatility);

    for (int t = 0; t < binomial_tree_.numTimesteps(); ++t) {
      // Begin by setting the spine. For vol surfaces with no smile, the
      // iteration order doesn't matter, apart from possible performance, but
      // for local vol models, this is essential.
      if (t % 2 == 0) {
        binomial_tree_.setValue(
            t, t / 2, propagator_(binomial_tree_, volatility, t, t / 2));
      } else {
        binomial_tree_.setValue(
            t,
            (t + 1) / 2,
            propagator_(binomial_tree_, volatility, t, (t + 1) / 2));
        binomial_tree_.setValue(
            t,
            (t - 1) / 2,
            propagator_(binomial_tree_, volatility, t, (t - 1) / 2));
      }

      // Fix all the nodes above the spine.
      for (int i = std::floor((t + 2) / 2); i <= t; ++i) {
        binomial_tree_.setValue(
            t, i, propagator_(binomial_tree_, volatility, t, i));
      }
      // And then below the spine.
      for (int i = std::floor((t - 2) / 2); i >= 0; --i) {
        double node_value = propagator_(binomial_tree_, volatility, t, i);
        if (node_value < 0) {
          LOG(WARNING) << "Node is negative!";
        }
        binomial_tree_.setValue(t, i, node_value);
      }
    }
  }

  void forwardPropagate() {
    for (int t = 0; t < binomial_tree_.numTimesteps(); t++) {
      for (int i = 0; i <= t; ++i) {
        binomial_tree_.setValue(t, i, propagator_(binomial_tree_, t, i));
      }
    }
  }

  void updateSpot(double spot) { propagator_.updateSpot(spot); }

  const BinomialTree& binomialTree() const { return binomial_tree_; }

 private:
  BinomialTree binomial_tree_;
  PropagatorT propagator_;
};

}  // namespace markets

#endif  // MARKETS_BINOMIAL_TREE_H_