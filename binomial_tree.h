#ifndef MARKETS_BINOMIAL_TREE_H_
#define MARKETS_BINOMIAL_TREE_H_

#include <Eigen/Dense>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

#include "markets/time.h"
#include "markets/volatility.h"
#include "time.h"

namespace markets {

class BinomialTree {
 public:
  BinomialTree(double total_duration_years, double timestep_years)
      : tree_duration_years_(total_duration_years),
        timestep_years_(timestep_years) {
    int num_timesteps = std::ceil(tree_duration_years_ / timestep_years_) + 1;
    tree_.resize(num_timesteps, num_timesteps);
    tree_.setZero();
  }

  BinomialTree() {}

  // Helper factory functions using the chrono library.
  static BinomialTree create(std::chrono::years total_duration,
                             std::chrono::weeks timestep,
                             YearStyle style = YearStyle::k365) {
    return BinomialTree(total_duration.count(),
                        timestep.count() * 7 / numDaysInYear(style));
  }
  static BinomialTree create(std::chrono::months total_duration,
                             std::chrono::days timestep,
                             YearStyle style = YearStyle::k365) {
    return BinomialTree(total_duration.count() / 12.0,
                        timestep.count() / numDaysInYear(style));
  }

  static BinomialTree createFrom(const BinomialTree& underlying) {
    BinomialTree derived = underlying;
    derived.tree_.setZero();
    return derived;
  }

  int numTimesteps() const {
    // Subtract 1, because the number of timesteps represents the number of
    // differences (dt's)
    return tree_.rows() - 1;
  }

  void setInitValue(double val) { setValue(0, 0, val); }

  template <typename PropagatorT, typename VolatilityT>
  void forwardPropagate(const PropagatorT& fwd_prop, const VolatilityT& vol) {
    resizeWithTimeDependentVol(vol);
    for (int t = 0; t < numTimesteps(); t++) {
      for (int i = 0; i <= t; ++i) {
        setValue(t, i, fwd_prop(*this, vol, t, i));
      }
    }
  }

  template <typename PropagatorT>
  void forwardPropagate(const PropagatorT& fwd_prop) {
    for (int t = 0; t < numTimesteps(); t++) {
      for (int i = 0; i <= t; ++i) {
        setValue(t, i, fwd_prop(*this, t, i));
      }
    }
  }

  // Returns the risk-neutral, no-arbitrage up-probability at time index t and
  // state i.
  double getUpProbAt(int t, int i) const;

  void backPropagate(const BinomialTree& diffusion,
                     const std::function<double(double)>& payoff_fn,
                     double expiry_years);

  double sumAtTimestep(int t) const { return tree_.row(t).sum(); }

  void printAtTime(int t) const {
    std::cout << "Time " << t << ": ";
    std::cout << tree_.row(t) << std::endl;
  }
  void printUpTo(int ti) const {
    for (int i = 0; i < ti; ++i) {
      std::cout << "t:" << i << " ::  " << tree_.row(i).head(i + 1)
                << std::endl;
    }
  }

  void printProbabilitiesUpTo(int ti) const {
    for (int t = 0; t < ti; ++t) {
      std::cout << "t:" << t << " q:";
      for (int i = 0; i <= t; ++i) {
        std::cout << " " << getUpProbAt(t, i);
      }
      std::cout << std::endl;
    }
  }

  double nodeValue(int time, int node_index) const {
    return tree_(time, node_index);
  }

  bool isTreeEmptyAt(int t) const {
    // current assumption: if an entire row is 0, nothing after it can be
    // populated.
    return tree_.row(t).isZero(0);
  }

  const Timegrid& getTimegrid() const { return timegrid_; }

  double exactTimestepInYears() const { return timestep_years_; }
  double totalTimeAtIndex(int ti) const { return timegrid_.time(ti); }
  double timestepAt(int ti) const { return timegrid_.dt(ti); }
  double treeDurationYears() const { return tree_duration_years_; }

 private:
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> tree_;
  double tree_duration_years_;
  double timestep_years_;

  Timegrid timegrid_;

  void setValue(int time, int node_index, double val) {
    tree_(time, node_index) = val;
  }

  template <typename VolSurfaceT>
  void resizeWithTimeDependentVol(const Volatility<VolSurfaceT>& volfn) {
    timegrid_ = volfn.generateTimegrid(tree_duration_years_, timestep_years_);
    tree_.resize(timegrid_.size(), timegrid_.size());
    tree_.setZero();
  }
};

inline std::vector<Eigen::Vector2d> getNodes(const BinomialTree& tree) {
  std::vector<Eigen::Vector2d> nodes;
  for (int t = 0; t < tree.numTimesteps(); ++t) {
    if (tree.isTreeEmptyAt(t)) {
      break;
    }
    for (int i = 0; i <= t; ++i) {
      nodes.emplace_back(
          Eigen::Vector2d{tree.totalTimeAtIndex(t), tree.nodeValue(t, i)});
    }
  }
  return nodes;
}

struct TreeRenderData {
  std::vector<double> x_coords, y_coords, edge_x_coords, edge_y_coords;
};

inline TreeRenderData legacy_getTreeRenderData(const BinomialTree& tree) {
  TreeRenderData r;
  const auto nodes = getNodes(tree);
  for (const auto& node : nodes) {
    r.x_coords.push_back(node.x());
    r.y_coords.push_back(node.y());
  }

  int cumul_start_index = 0;
  for (int t = 0; t < tree.numTimesteps(); ++t) {
    for (int i = 0; i <= t; ++i) {
      if (t < tree.numTimesteps()) {
        size_t parentIndex = cumul_start_index + i;
        size_t child1Index = cumul_start_index + t + i + 0;
        size_t child2Index = cumul_start_index + t + i + 1;

        if (child1Index < nodes.size()) {
          // Add coordinates for the segment
          r.edge_x_coords.push_back(nodes[parentIndex].x());
          r.edge_y_coords.push_back(nodes[parentIndex].y());
          r.edge_x_coords.push_back(nodes[child1Index].x());
          r.edge_y_coords.push_back(nodes[child1Index].y());
        }
        if (child2Index < nodes.size()) {
          // Add coordinates for the segment
          r.edge_x_coords.push_back(nodes[parentIndex].x());
          r.edge_y_coords.push_back(nodes[parentIndex].y());
          r.edge_x_coords.push_back(nodes[child2Index].x());
          r.edge_y_coords.push_back(nodes[child2Index].y());
        }
      }
    }
    cumul_start_index += t;
  }
  return r;
}

inline TreeRenderData getTreeRenderData(const BinomialTree& tree) {
  TreeRenderData r;

  // First loop to collect node coordinates
  for (int t = 0; t < tree.numTimesteps(); ++t) {
    if (tree.isTreeEmptyAt(t)) {
      break;
    }
    for (int i = 0; i <= t; ++i) {
      r.x_coords.push_back(tree.totalTimeAtIndex(t));
      r.y_coords.push_back(tree.nodeValue(t, i));
    }
  }

  // Second loop to add edge coordinates
  int cumul_start_index = 0;
  for (int t = 0; t < tree.numTimesteps() - 1;
       ++t) {  // Iterate up to second-to-last timestep
    if (tree.isTreeEmptyAt(t)) {
      break;
    }
    for (int i = 0; i <= t; ++i) {
      // Calculate child indices
      size_t child1Index = cumul_start_index + t + i + 1;
      size_t child2Index = child1Index + 1;

      if (child1Index < r.x_coords.size()) {  // Check if child 1 exists
        // Add edge coordinates for child 1
        r.edge_x_coords.push_back(tree.totalTimeAtIndex(t));
        r.edge_y_coords.push_back(tree.nodeValue(t, i));
        r.edge_x_coords.push_back(tree.totalTimeAtIndex(t + 1));
        r.edge_y_coords.push_back(tree.nodeValue(t + 1, i));
      }
      if (child2Index < r.x_coords.size()) {  // Check if child 2 exists
        // Add edge coordinates for child 2
        r.edge_x_coords.push_back(tree.totalTimeAtIndex(t));
        r.edge_y_coords.push_back(tree.nodeValue(t, i));
        r.edge_x_coords.push_back(tree.totalTimeAtIndex(t + 1));
        r.edge_y_coords.push_back(tree.nodeValue(t + 1, i + 1));
      }
    }
    cumul_start_index += t + 1;
  }

  return r;
}

class AssetTree {};

}  // namespace markets

#endif  // MARKETS_BINOMIAL_TREE_H_