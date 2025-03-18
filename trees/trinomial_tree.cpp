#include "trinomial_tree.h"

#include <cmath>
#include <numeric>

namespace smileexplorer {

BranchProbabilities::BranchProbabilities(double a,
                                         double dt,
                                         int j,
                                         TrinomialBranchStyle branch_style) {
  // These equations are from page 742 in John Hull, "Options..." (11th ed.)
  const double ajdt = a * j * dt;
  const double ajdt_sqrd = std::pow(ajdt, 2);
  if (branch_style == TrinomialBranchStyle::Centered) {
    pu = (1. / 6) + 0.5 * (ajdt_sqrd - ajdt);
    pm = (2. / 3) - ajdt_sqrd;
    pd = (1. / 6) + 0.5 * (ajdt_sqrd + ajdt);
  } else if (branch_style == TrinomialBranchStyle::SlantedUp) {
    pu = (1. / 6) + 0.5 * (ajdt_sqrd + ajdt);
    pm = (-1. / 3) - ajdt_sqrd - 2 * ajdt;
    pd = (7. / 6) + 0.5 * (ajdt_sqrd + 3 * ajdt);
  } else {
    pu = (7. / 6) + 0.5 * (ajdt_sqrd - 3 * ajdt);
    pm = (-1. / 3) - ajdt_sqrd + 2 * ajdt;
    pd = (1. / 6) + 0.5 * (ajdt_sqrd - ajdt);
  }
}

// Returns threshold for which probabilities are always positive.
int TrinomialTree::jMax() const {
  // See section 32.5 in John Hull, "Options..." (11th ed.) (or Hull & White,
  // 1994) for the explanation of this hard-coded constant.
  constexpr double branching_switchover_multiplier = 0.184;
  return std::ceil(branching_switchover_multiplier / (a_ * dt_));
}

int TrinomialTree::numStatesWithClamping(int time_index) const {
  int unclamped_num_states = TrinomialTree::unclampedNumStates(time_index);
  return std::min(jMax() * 2 + 1, unclamped_num_states);
}

// Returns true if the timeslice would have had more states, but due to the
// jMax clamping factor, this timeslice has a narrower range of states.
bool TrinomialTree::isTimesliceClamped(int time_index) const {
  int unclamped_num_states = TrinomialTree::unclampedNumStates(time_index);
  return numStatesWithClamping(time_index) < unclamped_num_states ||
         numStatesWithClamping(time_index) ==
             numStatesWithClamping(time_index + 1);
}

TrinomialBranchStyle TrinomialTree::getBranchStyleForNode(int time_index,
                                                          int j) const {
  if (j <= -jMax() && isTimesliceClamped(time_index)) {
    return TrinomialBranchStyle::SlantedUp;
  } else if (j >= jMax() && isTimesliceClamped(time_index)) {
    return TrinomialBranchStyle::SlantedDown;
  }
  return TrinomialBranchStyle::Centered;
}

void TrinomialTree::firstStage() {
  for (size_t ti = 0; ti < tree_.size(); ++ti) {
    int num_states = numStatesWithClamping(ti);
    int shift = (num_states - 1) / 2;

    for (int i = 0; i < num_states; ++i) {
      // i is the index for indexing into the vector of states.
      // However, we convert them to j, which is centered at 0.
      int curr_j = i - shift;
      tree_[ti].push_back(TrinomialNode(
          a_, dt_, curr_j, sigma_, getBranchStyleForNode(ti, curr_j)));
    }
  }
}

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

void TrinomialTree::secondStage(const ZeroSpotCurve& market_curve) {
  alphas_[0] = market_curve.forwardRate(0, dt_);
  tree_[0][0].arrow_debreu = 1.;

  for (size_t ti = 0; ti < tree_.size() - 1; ++ti) {
    // Iterate over each node in the current timeslice once...
    for (int j = 0; j < std::ssize(tree_[ti]); ++j) {
      auto& curr_node = tree_[ti][j];
      // ... and for each iteration, update the 3 successor nodes in the next
      // timestep.
      //
      // Note that the formula in John Hull, "Options..." (pg 745,
      // formula 32.12) writes this a bit differently, as a single expression
      // for Q_{m+1,j}, as a sum which iterates over all Q_{m} which can lead to
      // this node. Mathematically that is more concise, but for the
      // implementation it would be more complicated because the number of
      // incoming edges can vary greatly, whereas the number of outgoing edges
      // is always 3.
      updateSuccessorNodes(curr_node, ti, j, alphas_[ti], dt_);
    }

    alphas_[ti + 1] = std::log(arrowDebreuSumAtTimestep(ti + 1) /
                               market_curve.df(dt_ * (ti + 2))) /
                      dt_;
  }
}

}  // namespace smileexplorer