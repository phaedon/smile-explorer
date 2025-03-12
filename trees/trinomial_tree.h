#ifndef SMILEEXPLORER_TREES_TRINOMIAL_TREE_H_
#define SMILEEXPLORER_TREES_TRINOMIAL_TREE_H_

#include <numbers>
#include <vector>

#include "rates/zero_curve.h"

namespace smileexplorer {

// Returns the spacing between interest rates on the tree.
inline double dR(double sigma, double dt) {
  return sigma * std::numbers::sqrt3 * std::sqrt(dt);
}

enum class TrinomialBranchStyle { Centered, SlantedUp, SlantedDown };

struct BranchProbabilities {
  BranchProbabilities(double a,
                      double dt,
                      int j,
                      TrinomialBranchStyle branch_style);
  double pu, pm, pd;
};

struct TrinomialNode {
  TrinomialNode(double a,
                double dt,
                int j,
                double sigma,
                TrinomialBranchStyle branch_style)
      : arrow_deb(0),
        val(j * dR(sigma, dt)),
        branch_style(branch_style),
        branch_probs(BranchProbabilities(a, dt, j, branch_style)) {}

  double arrow_deb;
  double val;
  TrinomialBranchStyle branch_style;
  BranchProbabilities branch_probs;
};

using TrinomialTimeslice = std::vector<TrinomialNode>;

class TrinomialTree {
 public:
  TrinomialTree(double a, double dt, double sigma)
      : a_(a), dt_(dt), sigma_(sigma) {}

  void forwardPropagate(const ZeroSpotCurve& market_curve) {
    firstStage();
    secondStage(market_curve);
  }
  void firstStage();
  void secondStage(const ZeroSpotCurve& market_curve);

  double a_, dt_, sigma_;
  std::vector<TrinomialTimeslice> tree_;
  std::vector<double> alphas_;

 private:
  void updateSuccessorNodes(const TrinomialNode& curr_node,
                            int time_index,
                            int j,  // curr_node's state index
                            double alpha,
                            double dt);

  // Returns threshold for which probabilities are always positive.
  int jMax() const;

  int numStatesWithClamping(int time_index) const;

  // Returns true if the timeslice would have had more states, but due to the
  // jMax clamping factor, this timeslice has a narrower range of states.
  bool isTimesliceClamped(int time_index) const;

  TrinomialBranchStyle getBranchStyleForNode(int time_index, int j) const;
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_TREES_TRINOMIAL_TREE_H_