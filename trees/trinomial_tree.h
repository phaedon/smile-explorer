#ifndef SMILEEXPLORER_TREES_TRINOMIAL_TREE_H_
#define SMILEEXPLORER_TREES_TRINOMIAL_TREE_H_

#include <iostream>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <vector>

#include "time/time_enums.h"
#include "time/timegrid.h"

namespace smileexplorer {

// Returns the spacing between interest rates on the tree.
inline double dR(double sigma, double dt) {
  return sigma * std::numbers::sqrt3 * std::sqrt(dt);
}

enum class TrinomialBranchStyle { Centered, SlantedUp, SlantedDown };

struct BranchProbabilities {
  BranchProbabilities() = default;
  BranchProbabilities(double a,
                      double dt,
                      int j,
                      TrinomialBranchStyle branch_style);
  double pu, pm, pd;
};

struct ForwardRateCache {
  std::optional<double> operator()(ForwardRateTenor tenor) const {
    if (cache.contains(tenor)) return cache.at(tenor);
    return std::nullopt;
  }
  std::unordered_map<ForwardRateTenor, double> cache;
};

struct TrinomialNode {
  TrinomialNode(double state_val,
                TrinomialBranchStyle style,
                BranchProbabilities probs)
      : arrow_debreu(0),
        state_value(state_val),
        // auxiliary_value(0),
        branch_style(style),
        branch_probs(probs) {}

  double arrow_debreu;
  double state_value;
  // double auxiliary_value;

  TrinomialBranchStyle branch_style;
  BranchProbabilities branch_probs;
  ForwardRateCache forward_rate_cache;
};

template <typename NodeT>
struct NodeTriplet {
  NodeT& up;
  NodeT& mid;
  NodeT& down;
};

using TrinomialTimeslice = std::vector<TrinomialNode>;

class TrinomialTree {
 public:
  TrinomialTree(double tree_duration_years, double dt)
      : tree_duration_years_(tree_duration_years), dt_(dt) {
    int num_timesteps = std::ceil(tree_duration_years_ / dt_) + 1;
    tree_.resize(num_timesteps);
    alphas_.resize(num_timesteps);

    Timegrid grid(num_timesteps);
    for (int i = 0; i < num_timesteps; ++i) {
      grid.set(i, i * dt_);
    }
    timegrid_ = grid;
  }

  static int unclampedNumStates(int time_index) { return time_index * 2 + 1; }

  int numStatesAt(int time_index) const { return tree_[time_index].size(); }

  bool isTimesliceClamped(int time_index) const {
    return numStatesAt(time_index) <
               TrinomialTree::unclampedNumStates(time_index) ||
           numStatesAt(time_index) == numStatesAt(time_index + 1);
  }

  // TODO: Move this to ShortRateTreeCurve once curve-fitting (secondStage and
  // forwardPropagate) have been moved out of this library, which should only be
  // concerned with the core data structure.
  static TrinomialTree create(double tree_duration_years,
                              ForwardRateTenor fra_tenor,
                              int tenor_subdivisions) {
    // The smallest timestep must fit within the target tenor.
    if (tenor_subdivisions < 1) {
      tenor_subdivisions = 1;
    }

    // For example, a 3m tenor and 6 subdivisions results in half month
    // intervals: 3/(6*12) == 1/24 of a year.
    double dt = static_cast<int>(fra_tenor) /
                (tenor_subdivisions * static_cast<double>(kNumMonthsPerYear));
    return TrinomialTree(tree_duration_years, dt);
  }

  static TrinomialTree createFrom(const TrinomialTree& underlying) {
    TrinomialTree derived = underlying;
    derived.setZeroAfterIndex(-1);
    return derived;
  }

  NodeTriplet<const TrinomialNode> getSuccessorNodes(
      const TrinomialNode& curr_node, int time_index, int j) const;

  NodeTriplet<TrinomialNode> getSuccessorNodes(const TrinomialNode& curr_node,
                                               int time_index,
                                               int j);

  double totalTimeAtIndex(int time_index) const { return dt_ * (time_index); }

  double shortRate(int time_index, int state_index) const {
    return tree_[time_index][state_index].state_value + alphas_[time_index];
  }

  const Timegrid& getTimegrid() const { return timegrid_; }

  double arrowDebreuSumAtTimestep(int time_index) const;

  void copyNodeValuesAtTimeIndex(int time_index, const TrinomialTree& other) {
    for (size_t i = 0; i < tree_[time_index].size(); ++i) {
      tree_[time_index][i].state_value = other.tree_[time_index][i].state_value;
    }
  }

  void setNodeValuesAtTimeIndex(int time_index, double value) {
    for (auto& node : tree_[time_index]) {
      node.state_value = value;
    }
  }

  void setZeroAfterIndex(int time_index) {
    for (size_t ti = time_index + 1; ti < tree_.size(); ++ti) {
      for (auto& node : tree_[ti]) {
        node.state_value = 0;
      }
    }
  }

  void clearNodeValues() {
    for (size_t ti = 0; ti < tree_.size(); ++ti) {
      setNodeValuesAtTimeIndex(ti, 0.);
    }
  }

  const TrinomialNode& node(int time_index, int state_index) const {
    return tree_[time_index][state_index];
  }

  double nodeValue(int time_index, int state_index) const {
    return node(time_index, state_index).state_value;
  }

  void setNodeValue(int time_index, int state_index, double value) {
    tree_[time_index][state_index].state_value = value;
  }

  void setProbabilityWeightedNodeValue(int time_index,
                                       int state_index,
                                       double value) {
    double arrow_debreu_sum = 0;
    for (const auto& node : tree_[time_index]) {
      arrow_debreu_sum += node.arrow_debreu;
    }
    double probability =
        tree_[time_index][state_index].arrow_debreu / arrow_debreu_sum;
    tree_[time_index][state_index].state_value = value * probability;
  }

  // double auxiliaryValue(int time_index, int state_index) const {
  //   return tree_[time_index][state_index].auxiliary_value;
  // }

  // void setAuxiliaryValue(int time_index, int state_index, double value) {
  //   tree_[time_index][state_index].auxiliary_value = value;
  // }

  // Useful for visualisation.
  bool isTreeEmptyAt(size_t time_index) const {
    // current assumption: if an entire timeslice is 0, nothing after it can be
    // populated.
    for (const auto& node : tree_[time_index]) {
      if (node.state_value != 0.0) return false;
    }
    return true;
  }

  int timestepsPerForwardRateTenor(ForwardRateTenor tenor) {
    // For example, a 3m tenor and 6 subdivisions results in half month
    // intervals:
    //     3 / (6 * 12) == 1/24 of a year.
    // So we reconstitute the number of subdivisions by doing:
    //     3 / (.0416667 * 12) == 3*24/12 == 6
    return std::round(static_cast<int>(tenor) / (dt_ * kNumMonthsPerYear));
  }

  void printUpTo(size_t time_index) const {
    for (size_t ti = 0; ti < std::min(time_index, tree_.size()); ++ti) {
      std::cout << "ti:" << ti << " ::  ";
      for (int j = 0; j < numStatesAt(ti); ++j) {
        std::cout << "  " << tree_[ti][j].state_value;
      }
      std::cout << std::endl;
    }
  }

  void updateSuccessorNodes(const TrinomialNode& curr_node,
                            int time_index,
                            int j,  // curr_node's state index
                            double alpha,
                            double dt);

 private:
  double tree_duration_years_;

 public:
  double dt_;
  std::vector<TrinomialTimeslice> tree_;

  // TODO: Consider moving this out and into the ShortRateTreeCurve.
  std::vector<double> alphas_;

 private:
  Timegrid timegrid_;
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_TREES_TRINOMIAL_TREE_H_