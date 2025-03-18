#ifndef SMILEEXPLORER_TREES_HULL_WHITE_PROPAGATOR_H_
#define SMILEEXPLORER_TREES_HULL_WHITE_PROPAGATOR_H_

#include "trinomial_tree.h"

namespace smileexplorer {

// Returns threshold for which probabilities are always positive.
inline int jMax(double a, double dt) {
  // See section 32.5 in John Hull, "Options..." (11th ed.) (or Hull & White,
  // 1994) for the explanation of this hard-coded constant.
  constexpr double branching_switchover_multiplier = 0.184;
  return std::ceil(branching_switchover_multiplier / (a * dt));
}

inline BranchProbabilities computeHullWhiteBranchProbabilities(
    double a, double dt, int j, TrinomialBranchStyle branch_style) {
  BranchProbabilities probs;
  // These equations are from page 742 in John Hull, "Options..." (11th ed.)
  const double ajdt = a * j * dt;
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

inline TrinomialNode createHullWhiteTrinomialNode(
    double a,
    double dt,
    // j is the zero-centered state index:
    int j,
    double sigma,
    TrinomialBranchStyle branch_style) {
  double state_value = j * dR(sigma, dt);
  return TrinomialNode(
      state_value,
      branch_style,
      computeHullWhiteBranchProbabilities(a, dt, j, branch_style));
}

class HullWhitePropagator {
 public:
  // The timegrid spacing is provided in the constructor to indicate that this
  // class is only really intended to support constant spacing. Although
  // time-varying spacing may be supported in the future, this will require
  // additional changes, including:
  //  - time-dependent sigma (normal vol)
  //  - ensuring that trinomial branching is still recombining.
  HullWhitePropagator(double mean_reversion_speed, double sigma, double dt)
      : a_(mean_reversion_speed), sigma_(sigma), dt_(dt) {}

  // A trinomial tree may have full branching or may have model-specific
  // clamping. It's the model's role to provide this as part of the API.
  // Guaranteed to return an odd number.
  int numStatesAtTimeIndex(int time_index) const {
    return std::min(jMax(a_, dt_) * 2 + 1,
                    TrinomialTree::unclampedNumStates(time_index));
  }

  TrinomialBranchStyle getBranchStyleForNode(int time_index, int j) const {
    if (j <= -jMax(a_, dt_) && shouldClampTimeslice(time_index)) {
      return TrinomialBranchStyle::SlantedUp;
    } else if (j >= jMax(a_, dt_) && shouldClampTimeslice(time_index)) {
      return TrinomialBranchStyle::SlantedDown;
    }
    return TrinomialBranchStyle::Centered;
  }

  TrinomialNode operator()(int time_index, int state_index) const {
    // i is the index for indexing into the vector of states.
    // Convert the state_index to "j", which is centered at 0.
    // (Consistent with the nomenclature in the Hull-White papers.)
    int shift = (numStatesAtTimeIndex(time_index) - 1) / 2;
    int j = state_index - shift;

    return createHullWhiteTrinomialNode(
        a_, dt_, j, sigma_, getBranchStyleForNode(time_index, j));
  }

 private:
  double a_;
  double sigma_;
  double dt_;

  bool shouldClampTimeslice(int time_index) const {
    return numStatesAtTimeIndex(time_index) <
               TrinomialTree::unclampedNumStates(time_index) ||
           numStatesAtTimeIndex(time_index) ==
               numStatesAtTimeIndex(time_index + 1);
  }
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_TREES_HULL_WHITE_PROPAGATOR_H_