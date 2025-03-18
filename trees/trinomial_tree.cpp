#include "trinomial_tree.h"

#include <cmath>
#include <numeric>

namespace smileexplorer {

NodeTriplet<const TrinomialNode> TrinomialTree::getSuccessorNodes(
    const TrinomialNode& curr_node, int time_index, int j) const {
  if (!isTimesliceClamped(time_index) ||
      curr_node.branch_style == TrinomialBranchStyle::SlantedUp) {
    return NodeTriplet<const TrinomialNode>{tree_[time_index + 1][j + 2],
                                            tree_[time_index + 1][j + 1],
                                            tree_[time_index + 1][j + 0]};
  }

  if (curr_node.branch_style == TrinomialBranchStyle::SlantedDown) {
    return NodeTriplet<const TrinomialNode>{tree_[time_index + 1][j + 0],
                                            tree_[time_index + 1][j + -1],
                                            tree_[time_index + 1][j + -2]};
  }
  // Clamped && mid:
  return NodeTriplet<const TrinomialNode>{tree_[time_index + 1][j + 1],
                                          tree_[time_index + 1][j + 0],
                                          tree_[time_index + 1][j + -1]};
}

NodeTriplet<TrinomialNode> TrinomialTree::getSuccessorNodes(
    const TrinomialNode& curr_node, int time_index, int j) {
  if (!isTimesliceClamped(time_index) ||
      curr_node.branch_style == TrinomialBranchStyle::SlantedUp) {
    return NodeTriplet<TrinomialNode>{tree_[time_index + 1][j + 2],
                                      tree_[time_index + 1][j + 1],
                                      tree_[time_index + 1][j + 0]};
  }

  if (curr_node.branch_style == TrinomialBranchStyle::SlantedDown) {
    return NodeTriplet<TrinomialNode>{tree_[time_index + 1][j + 0],
                                      tree_[time_index + 1][j + -1],
                                      tree_[time_index + 1][j + -2]};
  }
  // Clamped && mid:
  return NodeTriplet<TrinomialNode>{tree_[time_index + 1][j + 1],
                                    tree_[time_index + 1][j + 0],
                                    tree_[time_index + 1][j + -1]};
}

void TrinomialTree::updateSuccessorNodes(const TrinomialNode& curr_node,
                                         int time_index,
                                         int j,  // curr_node's state index
                                         double alpha,
                                         double dt) {
  auto next = getSuccessorNodes(curr_node, time_index, j);
  const double df = std::exp(-(alpha + curr_node.state_value) * dt);

  double arrow_deb_up = curr_node.branch_probs.pu * df;
  double arrow_deb_mid = curr_node.branch_probs.pm * df;
  double arrow_deb_down = curr_node.branch_probs.pd * df;

  next.up.arrow_debreu += arrow_deb_up * curr_node.arrow_debreu;
  next.mid.arrow_debreu += arrow_deb_mid * curr_node.arrow_debreu;
  next.down.arrow_debreu += arrow_deb_down * curr_node.arrow_debreu;
}

double TrinomialTree::arrowDebreuSumAtTimestep(int time_index) const {
  const auto& timeslice = tree_[time_index];
  return std::accumulate(timeslice.begin(),
                         timeslice.end(),
                         0.0,
                         [](double ad_sum, const TrinomialNode& node) {
                           return ad_sum + node.arrow_debreu *
                                               std::exp(-node.state_value);
                         });
}

}  // namespace smileexplorer