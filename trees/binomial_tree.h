#ifndef MARKETS_BINOMIAL_TREE_H_
#define MARKETS_BINOMIAL_TREE_H_

#include <Eigen/Dense>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

#include "rates/rates_curve.h"
#include "time.h"
#include "volatility/volatility.h"

namespace markets {

class BinomialTree {
 public:
  BinomialTree(double total_duration_years, double timestep_years)
      :  // Clamp to 1 hour (to avoid going to 0).
        tree_duration_years_(std::max(total_duration_years, 1. / (24 * 365))),
        timestep_years_(std::min(timestep_years, tree_duration_years_)) {
    // Clamp to a 1-period tree in the case of a bad input for the timestep.
    if (timestep_years_ <= 0) {
      timestep_years_ = tree_duration_years_ * 0.5;
    }
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

  double sumAtTimestep(int time_index) const {
    return tree_.row(time_index).sum();
  }

  void printAtTime(int time_index) const {
    std::cout << "Time " << time_index << ": ";
    std::cout << tree_.row(time_index) << std::endl;
  }
  void printUpTo(int time_index) const {
    for (int i = 0; i < time_index; ++i) {
      std::cout << "t:" << i << " ::  " << tree_.row(i).head(i + 1)
                << std::endl;
    }
  }

  void setZeroAfterIndex(int time_index) {
    for (int i = time_index + 1; i < tree_.rows(); ++i) {
      tree_.row(i).setZero();
    }
  }

  double nodeValue(int time_index, int node_index) const {
    return tree_(time_index, node_index);
  }

  std::optional<double> safeNodeValue(int time_index, int node_index) const {
    if (time_index < 0 || time_index >= tree_.rows() || node_index < 0 ||
        node_index > time_index) {
      return std::nullopt;
    }
    return nodeValue(time_index, node_index);
  }

  bool isTreeEmptyAt(int time_index) const {
    // current assumption: if an entire row is 0, nothing after it can be
    // populated.
    return tree_.row(time_index).isZero(0);
  }

  const Timegrid& getTimegrid() const { return timegrid_; }

  double exactTimestepInYears() const { return timestep_years_; }
  double totalTimeAtIndex(int time_index) const {
    return timegrid_.time(time_index);
  }
  double timestepAt(int time_index) const { return timegrid_.dt(time_index); }
  double treeDurationYears() const { return tree_duration_years_; }

  void setValue(int time_index, int node_index, double val) {
    tree_(time_index, node_index) = val;
  }

  // TODO make this not take a vol, that makes it brittle.
  template <typename VolSurfaceT>
  void resizeWithTimeDependentVol(const Volatility<VolSurfaceT>& volfn) {
    timegrid_ = volfn.generateTimegrid(tree_duration_years_, timestep_years_);
    tree_.resize(timegrid_.size(), timegrid_.size());
    tree_.setZero();
  }

  double getUpProbAt(const RatesCurve& curve, int time_index, int i) const;

  void printProbTreeUpTo(const RatesCurve& curve, int ti) const {
    for (int t = 0; t <= ti; ++t) {
      for (int i = 0; i <= t; ++i) {
        std::cout << getUpProbAt(curve, t, i) << "  ";
      }
      std::cout << std::endl;
    }
  }

 private:
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> tree_;
  double tree_duration_years_;
  double timestep_years_;

  Timegrid timegrid_;
};

struct TreeRenderData {
  std::vector<double> x_coords, y_coords, edge_x_coords, edge_y_coords;
};

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

}  // namespace markets

#endif  // MARKETS_BINOMIAL_TREE_H_