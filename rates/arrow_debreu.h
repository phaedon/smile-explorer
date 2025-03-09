#ifndef SMILEEXPLORER_RATES_ARROW_DEBREU_H_
#define SMILEEXPLORER_RATES_ARROW_DEBREU_H_

#include "trees/binomial_tree.h"

namespace smileexplorer {

struct ArrowDebreauPropagator {
  ArrowDebreauPropagator(const BinomialTree& rate_tree, int num_timesteps)
      : num_timesteps_(num_timesteps), rate_tree_(rate_tree) {}
  double operator()(const BinomialTree& tree, int t, int i) const {
    if (t == 0) return 1.0;
    // TODO add compounding for actual time
    double act_365 = rate_tree_.exactTimestepInYears();
    double prev_down = i == 0 ? 0 : tree.nodeValue(t - 1, i - 1);
    double df_down =
        i == 0 ? 0 : 1 / (1 + act_365 * rate_tree_.nodeValue(t - 1, i - 1));
    double prev_up = i == t ? 0 : tree.nodeValue(t - 1, i);
    double df_up =
        i == t ? 0 : 1 / (1 + act_365 * rate_tree_.nodeValue(t - 1, i));
    // TODO this 0.5 business isn't quite right.
    return 0.5 * prev_up * df_up + 0.5 * prev_down * df_down;
  }

  int num_timesteps_;
  const BinomialTree& rate_tree_;
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_RATES_ARROW_DEBREU_H_
