#ifndef MARKETS_BINOMIAL_TREE_H_
#define MARKETS_BINOMIAL_TREE_H_

#include <Eigen/Dense>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

#include "markets/time.h"
#include "time.h"

namespace markets {

auto genUpFn(double annual_vol, std::chrono::days timestep) {
  double dt = static_cast<double>(timestep.count()) / 365.25;
  double u = std::exp(annual_vol * std::sqrt(dt));
  return [u](double curr) { return curr * u; };
}

auto genDownFn(double annual_vol, std::chrono::days timestep) {
  double dt = static_cast<double>(timestep.count()) / 365.25;
  double d = 1.0 / std::exp(annual_vol * std::sqrt(dt));
  return [d](double curr) { return curr * d; };
}

auto genUpFn(double annual_vol, std::chrono::weeks timestep) {
  double dt = static_cast<double>(timestep.count()) * 7 / 365.25;
  double u = std::exp(annual_vol * std::sqrt(dt));
  return [u](double curr) { return curr * u; };
}

auto genDownFn(double annual_vol, std::chrono::weeks timestep) {
  double dt = static_cast<double>(timestep.count()) * 7 / 365.25;
  double d = 1.0 / std::exp(annual_vol * std::sqrt(dt));
  return [d](double curr) { return curr * d; };
}

class BinomialTree {
 public:
  BinomialTree(std::chrono::years total_duration,
               std::chrono::weeks timestep,
               YearStyle style = YearStyle::k365)
      : tree_duration_years_(total_duration.count()),
        timestep_years_(timestep.count() * 7 / numDaysInYear(style)),
        year_style_(style) {
    int num_timesteps = std::ceil(tree_duration_years_ / timestep_years_) + 1;
    tree_.resize(num_timesteps, num_timesteps);
    tree_.setZero();
  }

  BinomialTree(std::chrono::months total_duration,
               std::chrono::days timestep,
               YearStyle style = YearStyle::k365)
      : tree_duration_years_(total_duration.count() / 12.0),
        timestep_years_(timestep.count() / numDaysInYear(style)),
        year_style_(style) {
    int num_timesteps = std::ceil(tree_duration_years_ / timestep_years_) + 1;
    tree_.resize(num_timesteps, num_timesteps);
    tree_.setZero();
  }

  int numTimesteps() const { return tree_.rows(); }

  void setInitValue(double val) { setValue(0, 0, val); }

  template <typename PropagatorT>
  void forwardPropagate(const PropagatorT& fwd_prop) {
    for (int t = 0; t < numTimesteps(); t++) {
      for (int i = 0; i <= t; ++i) {
        setValue(t, i, fwd_prop(*this, t, i));
      }
    }
  }

  double sumAtTimestep(int t) const { return tree_.row(t).sum(); }

  void print() const { std::cout << tree_; }

  double nodeValue(int time, int node_index) const {
    return tree_(time, node_index);
  }

  std::chrono::days timestep() const {
    int rounded_days = std::round(timestep_years_ * numDaysInYear(year_style_));
    return std::chrono::days(rounded_days);
  }

  double exactTimestepInYears() const { return timestep_years_; }

 private:
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> tree_;
  double tree_duration_years_;
  double timestep_years_;
  YearStyle year_style_;

  void setValue(int time, int node_index, double val) {
    tree_(time, node_index) = val;
  }
};

inline std::vector<Eigen::Vector2d> getNodes(const BinomialTree& tree) {
  std::vector<Eigen::Vector2d> nodes;
  for (int t = 0; t < tree.numTimesteps(); ++t) {
    for (int i = 0; i <= t; ++i) {
      nodes.emplace_back(Eigen::Vector2d{t, tree.nodeValue(t, i)});
    }
  }
  return nodes;
}

struct TreeRenderData {
  std::vector<double> x_coords, y_coords, edge_x_coords, edge_y_coords;
};

TreeRenderData getTreeRenderData(const BinomialTree& tree) {
  TreeRenderData r;
  const auto nodes = getNodes(tree);
  for (const auto& node : nodes) {
    r.x_coords.push_back(node.x());
    r.y_coords.push_back(node.y());
  }

  for (size_t i = 0; i < nodes.size(); ++i) {
    int t = static_cast<int>(nodes[i].x());
    if (t < tree.numTimesteps() - 1) {
      size_t child1Index = i + t + 1;
      size_t child2Index = i + t + 2;

      if (child1Index < nodes.size()) {
        // Add coordinates for the segment
        r.edge_x_coords.push_back(nodes[i].x());
        r.edge_y_coords.push_back(nodes[i].y());
        r.edge_x_coords.push_back(nodes[child1Index].x());
        r.edge_y_coords.push_back(nodes[child1Index].y());
      }
      if (child2Index < nodes.size()) {
        // Add coordinates for the segment
        r.edge_x_coords.push_back(nodes[i].x());
        r.edge_y_coords.push_back(nodes[i].y());
        r.edge_x_coords.push_back(nodes[child2Index].x());
        r.edge_y_coords.push_back(nodes[child2Index].y());
      }
    }
  }
  return r;
}

struct CRRPropagator {
  CRRPropagator(double expected_drift, double annualized_vol, double spot_price)
      : expected_drift_(expected_drift),
        annualized_vol_(annualized_vol),
        spot_price_(spot_price) {}

  double operator()(const BinomialTree& tree, int t, int i) const {
    if (t == 0) return spot_price_;
    double u = annualized_vol_ * sqrt(tree.exactTimestepInYears());

    if (i == 0) {
      double d = -u;
      return tree.nodeValue(t - 1, 0) * std::exp(d);
    }

    return tree.nodeValue(t - 1, i - 1) * std::exp(u);
  }

  void updateVol(double vol) { annualized_vol_ = vol; }

  double expected_drift_;
  double annualized_vol_;
  double spot_price_;
};

struct JarrowRuddPropagator {
  JarrowRuddPropagator(double expected_drift,
                       double annualized_vol,
                       double spot_price)
      : expected_drift_(expected_drift),
        annualized_vol_(annualized_vol),
        spot_price_(spot_price) {}

  double operator()(const BinomialTree& tree, int t, int i) const {
    if (t == 0) return spot_price_;
    double dt = tree.exactTimestepInYears();

    if (i == 0) {
      double d = expected_drift_ * dt - annualized_vol_ * sqrt(dt);
      return tree.nodeValue(t - 1, 0) * std::exp(d);
    } else {
      double u = expected_drift_ * dt + annualized_vol_ * sqrt(dt);
      return tree.nodeValue(t - 1, i - 1) * std::exp(u);
    }
  }

  void updateVol(double vol) { annualized_vol_ = vol; }

  double expected_drift_;
  double annualized_vol_;
  double spot_price_;
};

class AssetTree {};

}  // namespace markets

#endif  // MARKETS_BINOMIAL_TREE_H_