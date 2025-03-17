#ifndef SMILEEXPLORER_DERIVATIVES_FORWARD_RATE_AGREEMENT_H_
#define SMILEEXPLORER_DERIVATIVES_FORWARD_RATE_AGREEMENT_H_

#include "trees/trinomial_tree.h"

namespace smileexplorer {

// In the case of a deterministic yield curve (non-stochastic rates),
// the computation is trivial and this class is unnecessary. This is also true
// of a properly calibrated short-rate tree, since we can sum up the
// Arrow-Debreu prices at the start-time and end-time (to obtain the two
// discount factors); their ratio is the forward rate over the period in
// question.
//
// However, for things like options on forward rates, we need to be able to
// compute the *conditional* forward rate at {time ti, state j}. This class
// enables us to reuse the backward induction in the Derivative class to compute
// these conditional rates.
struct ForwardRateAgreement {
  double operator()(const TrinomialTree& short_rate_tree,
                    int ti,
                    int j,
                    int ti_final) const {
    // This represents the money actually received at the end of the
    // accrual period.
    if (ti == ti_final) {
      return 1.0;
    }

    const auto& curr_deriv_node = short_rate_tree.tree_[ti][j];
    const auto& next =
        short_rate_tree.getSuccessorNodes(curr_deriv_node, ti, j);
    double expected_next =
        next.up.auxiliary_value * curr_deriv_node.branch_probs.pu +
        next.mid.auxiliary_value * curr_deriv_node.branch_probs.pm +
        next.down.auxiliary_value * curr_deriv_node.branch_probs.pd;

    double r = short_rate_tree.shortRate(ti, j);
    return std::exp(-r * short_rate_tree.dt_) * expected_next;
  }
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_DERIVATIVES_FORWARD_RATE_AGREEMENT_H_