#ifndef SMILEEXPLORER_TREES_TRINOMIAL_TREE_H_
#define SMILEEXPLORER_TREES_TRINOMIAL_TREE_H_

#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

#include "rates/rates_curve.h"

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
  BranchProbabilities() = default;
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
  TrinomialNode(
      double a, double dt, double sigma, int curr_j, int jmax, bool is_clamped)
      : j(curr_j), arrow_deb(0) {
    if (j == -jmax && is_clamped) {
      branch_style = TrinomialBranchStyle::SlantedUp;
    } else if (j == jmax && is_clamped) {
      branch_style = TrinomialBranchStyle::SlantedDown;
    } else {
      branch_style = TrinomialBranchStyle::Centered;
    }

    val = j * dR(sigma, dt);
    branch_probs = BranchProbabilities(a, j, dt, branch_style);
  }

  int j;
  double arrow_deb;

  double val;
  bool is_set = false;
  TrinomialBranchStyle branch_style;
  BranchProbabilities branch_probs;
};

using TrinomialTimeslice = std::vector<TrinomialNode>;

class TrinomialTree {
 public:
  TrinomialTree(double a, double dt, double sigma)
      : a_(a), dt_(dt), sigma_(sigma) {}

  void forwardPropagate() {
    firstStage();
    secondStage();
  }
  void firstStage();
  void secondStage();

  double sumAtTimeIndex(int ti);

 public:
  double a_, dt_, sigma_;
  std::vector<TrinomialTimeslice> tree_;
  std::vector<double> alphas_;
  std::vector<double> zero_prices_;
};

inline void TrinomialTree::firstStage() {
  // TODO: Remove hardcoding of number of timeslices.
  alphas_.resize(6);
  tree_.resize(4);
  zero_prices_.resize(4);

  int jmax = j_max(a_, dt_);
  int jmin = -jmax;

  for (int ti = 0; ti < 4; ++ti) {
    int unclamped_num_states = ti * 2 + 1;
    int num_states = std::min(jmax * 2 + 1, unclamped_num_states);
    int shift = (num_states - 1) / 2;

    bool is_clamped = num_states >= unclamped_num_states;

    for (int i = 0; i < num_states; ++i) {
      // i is the index for indexing into the vector of states.
      // However, we convert them to i, which is centered at 0.
      int curr_j = i - shift;
      tree_[ti].push_back(
          TrinomialNode(a_, dt_, sigma_, curr_j, jmax, is_clamped));
    }
  }
}

inline void updateSuccessorNodes(TrinomialNode& curr_node,
                                 TrinomialNode& next_up,
                                 TrinomialNode& next_mid,
                                 TrinomialNode& next_down,
                                 double alpha,
                                 double dt) {
  const double df = std::exp(-(alpha + curr_node.val) * dt);

  double arrow_deb_up = curr_node.branch_probs.pu * df;
  double arrow_deb_mid = curr_node.branch_probs.pm * df;
  double arrow_deb_down = curr_node.branch_probs.pd * df;

  next_up.arrow_deb += arrow_deb_up * curr_node.arrow_deb;
  next_mid.arrow_deb += arrow_deb_mid * curr_node.arrow_deb;
  next_down.arrow_deb += arrow_deb_down * curr_node.arrow_deb;
}

inline void TrinomialTree::secondStage() {
  ZeroSpotCurve market_curve(
      {0.5, 1.0, 1.5, 2.0, 2.5, 3.0},
      {.0343, .03824, 0.04183, 0.04512, 0.04812, 0.05086});

  alphas_[0] = market_curve.forwardRate(0, dt_);
  zero_prices_[0] = 1.;
  tree_[0][0].arrow_deb = 1.;

  int jmax = j_max(a_, dt_);
  int jmin = -jmax;
  std::cout << "ENTERING LOOP" << std::endl;
  for (int ti = 0; ti < tree_.size() - 1; ++ti) {
    int num_states = tree_[ti].size();
    int shift = (num_states - 1) / 2;

    std::cout << "For time index: " << ti << " ...." << std::endl;
    std::cout << "alpha! .....  " << alphas_[ti] << std::endl;

    for (int j = 0; j < num_states; ++j) {
      auto& curr_node = tree_[ti][j];

      int j_shift = 0;  // Default: Centered.
      if (curr_node.branch_style == TrinomialBranchStyle::SlantedDown) {
        j_shift = -1;
      } else if (curr_node.branch_style == TrinomialBranchStyle::SlantedUp) {
        j_shift = 1;
      }
      auto& next_up = tree_[ti + 1][j + 2 + j_shift];
      auto& next_mid = tree_[ti + 1][j + 1 + j_shift];
      auto& next_down = tree_[ti + 1][j + 0 + j_shift];

      updateSuccessorNodes(
          curr_node, next_up, next_mid, next_down, alphas_[ti], dt_);
    }

    double alpha_next_sum = 0.;
    for (const TrinomialNode& next_node : tree_[ti + 1]) {
      alpha_next_sum += next_node.arrow_deb * std::exp(-next_node.val);
    }

    std::cout << "alpha_next_sum: " << alpha_next_sum << std::endl;
    alphas_[ti + 1] =
        std::log(alpha_next_sum / market_curve.df(dt_ * (ti + 2))) / dt_;
  }
}

inline double TrinomialTree::sumAtTimeIndex(int ti) {
  const auto& timeslice = tree_[ti];
  double ad_sum = 0;
  for (int i = 0; i < timeslice.size(); ++i) {
    ad_sum += timeslice[i].arrow_deb;
  }
  return ad_sum;
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_TREES_TRINOMIAL_TREE_H_