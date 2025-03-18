#ifndef SMILEEXPLORER_TREES_HULL_WHITE_PROPAGATOR_H_
#define SMILEEXPLORER_TREES_HULL_WHITE_PROPAGATOR_H_

#include "trinomial_tree.h"

namespace smileexplorer {

class HullWhitePropagator {
 public:
  // The timegrid spacing "dt" is provided in the constructor to indicate that
  // this class is only really intended to support constant spacing. Although
  // time-varying spacing may be supported in the future, this will require
  // additional changes, including:
  //  - time-dependent sigma (normal vol)
  //  - ensuring that trinomial branching is still recombining.
  HullWhitePropagator(double mean_reversion_speed, double sigma, double dt)
      : a_(mean_reversion_speed), sigma_(sigma), dt_(dt) {}

  // A trinomial tree may have full branching or may have model-specific
  // clamping. It's the model's role to provide this as part of the API.
  // Guaranteed to return an odd number.
  int numStatesAtTimeIndex(int time_index) const;

  TrinomialNode operator()(int time_index, int state_index) const {
    // i is the index for indexing into the vector of states.
    // Convert the state_index to "j", which is centered at 0.
    // (Consistent with the nomenclature in the Hull-White papers.)
    int shift = (numStatesAtTimeIndex(time_index) - 1) / 2;
    int j = state_index - shift;

    return createTrinomialNode(j, getBranchStyleForNode(time_index, j));
  }

  // Returns threshold for which probabilities are always positive.
  int jMax() const;

  TrinomialBranchStyle getBranchStyleForNode(int time_index, int j) const;

  BranchProbabilities computeBranchProbabilities(
      int j, TrinomialBranchStyle branch_style) const;

  // j is the zero-centered state index:
  TrinomialNode createTrinomialNode(int j,
                                    TrinomialBranchStyle branch_style) const;

  double dt() const { return dt_; }

 private:
  double a_;
  double sigma_;
  double dt_;

  double dR() const;
  bool shouldClampTimeslice(int time_index) const;
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_TREES_HULL_WHITE_PROPAGATOR_H_