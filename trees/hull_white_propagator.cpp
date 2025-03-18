#include "trees/hull_white_propagator.h"

#include <numbers>

namespace smileexplorer {

int HullWhitePropagator::numStatesAtTimeIndex(int time_index) const {
  return std::min(jMax() * 2 + 1,
                  TrinomialTree::unclampedNumStates(time_index));
}

// Returns the spacing between interest rates on the tree.
double HullWhitePropagator::dR() const {
  return sigma_ * std::numbers::sqrt3 * std::sqrt(dt_);
}

bool HullWhitePropagator::shouldClampTimeslice(int time_index) const {
  return numStatesAtTimeIndex(time_index) <
             TrinomialTree::unclampedNumStates(time_index) ||
         numStatesAtTimeIndex(time_index) ==
             numStatesAtTimeIndex(time_index + 1);
}

int HullWhitePropagator::jMax() const {
  // See section 32.5 in John Hull, "Options..." (11th ed.) (or Hull & White,
  // 1994) for the explanation of this hard-coded constant.
  constexpr double branching_switchover_multiplier = 0.184;
  return std::ceil(branching_switchover_multiplier / (a_ * dt_));
}

TrinomialBranchStyle HullWhitePropagator::getBranchStyleForNode(int time_index,
                                                                int j) const {
  if (j <= -jMax() && shouldClampTimeslice(time_index)) {
    return TrinomialBranchStyle::SlantedUp;
  } else if (j >= jMax() && shouldClampTimeslice(time_index)) {
    return TrinomialBranchStyle::SlantedDown;
  }
  return TrinomialBranchStyle::Centered;
}

TrinomialNode HullWhitePropagator::createTrinomialNode(
    int j, TrinomialBranchStyle branch_style) const {
  // j is the zero-centered state index:
  double state_value = j * dR();
  return TrinomialNode(
      state_value, branch_style, computeBranchProbabilities(j, branch_style));
}

BranchProbabilities HullWhitePropagator::computeBranchProbabilities(
    int j, TrinomialBranchStyle branch_style) const {
  BranchProbabilities probs;
  // These equations are from page 742 in John Hull, "Options..." (11th ed.)
  const double ajdt = a_ * j * dt_;
  const double ajdt_sqrd = std::pow(ajdt, 2);
  if (branch_style == TrinomialBranchStyle::Centered) {
    probs.pu = (1. / 6) + 0.5 * (ajdt_sqrd - ajdt);
    probs.pm = (2. / 3) - ajdt_sqrd;
    probs.pd = (1. / 6) + 0.5 * (ajdt_sqrd + ajdt);
  } else if (branch_style == TrinomialBranchStyle::SlantedUp) {
    probs.pu = (1. / 6) + 0.5 * (ajdt_sqrd + ajdt);
    probs.pm = (-1. / 3) - ajdt_sqrd - 2 * ajdt;
    probs.pd = (7. / 6) + 0.5 * (ajdt_sqrd + 3 * ajdt);
  } else {
    probs.pu = (7. / 6) + 0.5 * (ajdt_sqrd - 3 * ajdt);
    probs.pm = (-1. / 3) - ajdt_sqrd + 2 * ajdt;
    probs.pd = (1. / 6) + 0.5 * (ajdt_sqrd - ajdt);
  }
  return probs;
}

}  // namespace smileexplorer