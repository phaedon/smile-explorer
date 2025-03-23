#ifndef SMILEEXPLORER_TREES_TRINOMIAL_TREE_OPERATORS_H_
#define SMILEEXPLORER_TREES_TRINOMIAL_TREE_OPERATORS_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "rates/short_rate_tree_curve.h"
#include "trinomial_tree.h"

namespace smileexplorer {

inline bool treesHaveMatchingStructure(const TrinomialTree& a,
                                       const TrinomialTree& b) {
  if (a.tree_.size() != b.tree_.size() ||
      a.getTimegrid().size() != b.getTimegrid().size()) {
    return false;
  }

  for (size_t i = 0; i < a.tree_.size(); ++i) {
    if (a.numStatesAt(i) != b.numStatesAt(i)) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Runs backward induction on the values in `tree`, using the short rates
 * in `tree_curve` for the discounting. The two trinomial trees must have
 * matching structures (timesteps and states per timestep).
 *
 * @param tree_curve Input tree used for discounting.
 * @param tree Tree containing a set of values to be propagated backward.
 * @param final_time_index: The time index containing the values to be
 * back-propagated.
 * @param initial_time_index
 * @return absl::Status
 */
inline absl::Status runBackwardInduction(const ShortRateTreeCurve& tree_curve,
                                         TrinomialTree& tree,
                                         int final_time_index,
                                         int initial_time_index) {
  if (!treesHaveMatchingStructure(tree_curve.trinomialTree(), tree)) {
    return absl::FailedPreconditionError(
        "Tree for backward induction does not match structure of short-rate "
        "tree.");
  }

  if (initial_time_index < 0 || initial_time_index >= final_time_index) {
    return absl::FailedPreconditionError(
        "inital_time_index must be less than final_time_index in order to "
        "perform backward induction.");
  }

  if (final_time_index >= std::ssize(tree.tree_)) {
    return absl::FailedPreconditionError(
        "final_time_index cannot be larger than the number of timeslices on "
        "the tree!");
  }

  const auto& short_rate_tree = tree_curve.trinomialTree();
  for (int ti = final_time_index - 1; ti >= initial_time_index; --ti) {
    for (int i = 0; i < tree.numStatesAt(ti); ++i) {
      auto& curr_node = tree.tree_[ti][i];
      const auto& next_nodes = tree.getSuccessorNodes(curr_node, ti, i);

      const double expected_next =
          next_nodes.up.state_value * curr_node.branch_probs.pu +
          next_nodes.mid.state_value * curr_node.branch_probs.pm +
          next_nodes.down.state_value * curr_node.branch_probs.pd;

      const double r = short_rate_tree.shortRate(ti, i);

      // By using += we preserve any other values (such as intermediate coupon
      // payments) which may have been set somewhere in the window
      // [initial_time_index, final_time_index].
      curr_node.state_value +=
          std::exp(-r * short_rate_tree.getTimegrid().dt(ti)) * expected_next;
    }
  }

  return absl::OkStatus();
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_TREES_TRINOMIAL_TREE_OPERATORS_H_