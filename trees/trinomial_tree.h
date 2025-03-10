#ifndef SMILEEXPLORER_TREES_TRINOMIAL_TREE_H_
#define SMILEEXPLORER_TREES_TRINOMIAL_TREE_H_

#include <numbers>
#include <vector>

namespace smileexplorer {

// Returns threshold for which probabilities are always positive.
inline int j_max(double a, double dt) {
  // See section 32.5 in John Hull (or Hull & White, 1994) for the
  // explanation of this hard-coded constant.
  constexpr double branching_switchover_multiplier = 0.184;
  return std::ceil(branching_switchover_multiplier / (a * dt));
}

// Returns the spacing between interest rates on the tree.
inline double dR(double sigma, double dt) {
  return sigma * std::numbers::sqrt3 * std::sqrt(dt);
}

enum class TrinomialBranchStyle { Centered, SlantedUp, SlantedDown };

struct BranchProbabilities {
  BranchProbabilities(double a,
                      int j,
                      double dt,
                      TrinomialBranchStyle branch_style) {
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
  double pu, pm, pd;
};

struct TrinomialNode {
  double val;
  int j;
  bool is_set = false;
  TrinomialBranchStyle branch_style;
  BranchProbabilities branch_probs;
};

using TrinomialTimeslice = std::vector<TrinomialNode>;

class TrinomialTree {
 public:
 private:
  std::vector<TrinomialTimeslice> tree_;
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_TREES_TRINOMIAL_TREE_H_