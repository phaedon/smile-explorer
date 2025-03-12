#include "trinomial_tree.h"

#include <cmath>
#include <iostream>
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
  int unclamped_num_states = time_index * 2 + 1;
  return std::min(jMax() * 2 + 1, unclamped_num_states);
}

// Returns true if the timeslice would have had more states, but due to the
// jMax clamping factor, this timeslice has a narrower range of states.
bool TrinomialTree::isTimesliceClamped(int time_index) const {
  int unclamped_num_states = time_index * 2 + 1;
  return numStatesWithClamping(time_index) >= unclamped_num_states;
}

TrinomialBranchStyle TrinomialTree::getBranchStyleForNode(int time_index,
                                                          int j) const {
  if (j == -jMax() && isTimesliceClamped(time_index)) {
    return TrinomialBranchStyle::SlantedUp;
  } else if (j == jMax() && isTimesliceClamped(time_index)) {
    return TrinomialBranchStyle::SlantedDown;
  }
  return TrinomialBranchStyle::Centered;
}

void TrinomialTree::firstStage() {
  // TODO: Remove hardcoding of number of timeslices.
  alphas_.resize(6);
  tree_.resize(4);

  for (int ti = 0; ti < 4; ++ti) {
    int num_states = numStatesWithClamping(ti);
    int shift = (num_states - 1) / 2;

    for (int i = 0; i < num_states; ++i) {
      // i is the index for indexing into the vector of states.
      // However, we convert them to i, which is centered at 0.
      int curr_j = i - shift;
      tree_[ti].push_back(TrinomialNode(
          a_, dt_, curr_j, sigma_, getBranchStyleForNode(ti, curr_j)));
    }
  }
}

void TrinomialTree::updateSuccessorNodes(const TrinomialNode& curr_node,
                                         int time_index,
                                         int j,  // curr_node's state index
                                         double alpha,
                                         double dt) {
  int j_shift = 0;  // Default: Centered.
  if (curr_node.branch_style == TrinomialBranchStyle::SlantedDown) {
    j_shift = -1;
  } else if (curr_node.branch_style == TrinomialBranchStyle::SlantedUp) {
    j_shift = 1;
  }

  auto& next_up = tree_[time_index + 1][j + 2 + j_shift];
  auto& next_mid = tree_[time_index + 1][j + 1 + j_shift];
  auto& next_down = tree_[time_index + 1][j + 0 + j_shift];

  const double df = std::exp(-(alpha + curr_node.val) * dt);

  double arrow_deb_up = curr_node.branch_probs.pu * df;
  double arrow_deb_mid = curr_node.branch_probs.pm * df;
  double arrow_deb_down = curr_node.branch_probs.pd * df;

  next_up.arrow_deb += arrow_deb_up * curr_node.arrow_deb;
  next_mid.arrow_deb += arrow_deb_mid * curr_node.arrow_deb;
  next_down.arrow_deb += arrow_deb_down * curr_node.arrow_deb;
}

void TrinomialTree::secondStage(const ZeroSpotCurve& market_curve) {
  alphas_[0] = market_curve.forwardRate(0, dt_);
  tree_[0][0].arrow_deb = 1.;

  for (size_t ti = 0; ti < tree_.size() - 1; ++ti) {
    std::cout << "For time index: " << ti << " ...." << std::endl;
    std::cout << "alpha! .....  " << alphas_[ti] << std::endl;

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

    const auto& next_timeslice = tree_[ti + 1];
    double arrow_debreu_sum = std::accumulate(
        next_timeslice.begin(),
        next_timeslice.end(),
        0.0,
        [](double ad_sum, const TrinomialNode& next_node) {
          return ad_sum + next_node.arrow_deb * std::exp(-next_node.val);
        });

    alphas_[ti + 1] =
        std::log(arrow_debreu_sum / market_curve.df(dt_ * (ti + 2))) / dt_;
  }
}

}  // namespace smileexplorer