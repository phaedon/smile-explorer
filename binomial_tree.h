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

using TimeDepVolFn = std::function<double(double)>;

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

  BinomialTree(double total_duration_years,
               double timestep_years,
               YearStyle style = YearStyle::k365)
      : tree_duration_years_(total_duration_years),
        timestep_years_(timestep_years),
        year_style_(style) {
    int num_timesteps = std::ceil(tree_duration_years_ / timestep_years_) + 1;
    tree_.resize(num_timesteps, num_timesteps);
    tree_.setZero();
  }

  // Prepares a tree for time-dependent deterministic volaility
  // (term structure, but no skew).
  // Preserves timestep_years_ as the size of the initial timestep.
  void resizeWithTimeDependentVol(const TimeDepVolFn& vol_fn) {
    double total_time = 0;
    double dt_curr = timestep_years_;
    timesteps_.push_back(dt_curr);
    total_times_.push_back(total_time);

    while (total_time <= tree_duration_years_) {
      double sig_curr = vol_fn(total_time);
      total_time += dt_curr;
      double sig_next = vol_fn(total_time);
      double dt_next = sig_curr * sig_curr * dt_curr / (sig_next * sig_next);
      timesteps_.push_back(dt_next);
      total_times_.push_back(total_time);
      dt_curr = dt_next;
    }

    tree_.resize(timesteps_.size(), timesteps_.size());
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

  template <typename PropagatorT>
  void backPropagate(const BinomialTree& diffusion,
                     const PropagatorT& back_prop,
                     const std::function<double(double)>& payoff_fn,
                     double expiry_years) {
    int t_final = getTimeIndexForExpiry(expiry_years);
    for (int i = 0; i <= t_final; ++i) {
      setValue(t_final, i, payoff_fn(diffusion.nodeValue(t_final, i)));
    }

    for (int t = t_final - 1; t >= 0; --t) {
      for (int i = t; i >= 0; --i) {
        double up = nodeValue(t + 1, i + 1);
        double down = nodeValue(t + 1, i);
        double up_prob = back_prop.getUpProbAt(exactTimestepInYears(), t, i);
        double down_prob = 1 - up_prob;

        // TODO no discounting (yet)
        setValue(t, i, up * up_prob + down * down_prob);
      }
    }
  }

  int getTimeIndexForExpiry(double expiry_years) const {
    // for example if expiry=0.5 and timestep=1/12, then we should return 6.
    // if expiry=1/12 and timestep=1/365 then we should return 30 or 31
    // (depending on rounding convention)
    return std::round(expiry_years / timestep_years_);
  }

  double sumAtTimestep(int t) const { return tree_.row(t).sum(); }

  void print() const { std::cout << tree_; }
  void printAtTime(int t) const {
    std::cout << "Time " << t << ": ";
    std::cout << tree_.row(t) << std::endl;
  }

  double nodeValue(int time, int node_index) const {
    return tree_(time, node_index);
  }

  std::chrono::days approxTimestepInDays() const {
    int rounded_days = std::round(timestep_years_ * numDaysInYear(year_style_));
    return std::chrono::days(rounded_days);
  }

  YearStyle getYearStyle() const { return year_style_; }

  double exactTimestepInYears() const { return timestep_years_; }
  double totalTimeAtIndex(int t) const {
    if (total_times_.empty())
      return timestep_years_ * t;
    else
      return total_times_[t];
  }

  double timestepAt(int t) const {
    if (timesteps_.empty()) {
      return timestep_years_;
    }
    return timesteps_[t];
  }

 private:
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> tree_;
  double tree_duration_years_;
  double timestep_years_;
  YearStyle year_style_;

  // Only used in case of time-dep vol. There is a better way to structure this.
  std::vector<double> timesteps_;
  std::vector<double> total_times_;

  void setValue(int time, int node_index, double val) {
    tree_(time, node_index) = val;
  }
};

inline std::vector<Eigen::Vector2d> getNodes(const BinomialTree& tree) {
  std::vector<Eigen::Vector2d> nodes;
  for (int t = 0; t < tree.numTimesteps(); ++t) {
    for (int i = 0; i <= t; ++i) {
      // nodes.emplace_back(Eigen::Vector2d{t, tree.nodeValue(t, i)});
      nodes.emplace_back(
          Eigen::Vector2d{tree.totalTimeAtIndex(t), tree.nodeValue(t, i)});
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
    int t = tree.getTimeIndexForExpiry(nodes[i].x());
    // static_cast<int>(nodes[i].x());
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

  CRRPropagator(double expected_drift,
                double spot_price,
                const TimeDepVolFn& vol_fn)
      : expected_drift_(expected_drift),
        spot_price_(spot_price),
        vol_fn_(std::make_unique<const TimeDepVolFn>(vol_fn)) {}

  double getVol(double t) const {
    if (isVolConst()) {
      return annualized_vol_;
    }
    return (*vol_fn_)(t);
  }

  bool isVolConst() const { return vol_fn_ == nullptr; }

  double operator()(const BinomialTree& tree, int t, int i) const {
    if (t == 0) return spot_price_;
    double curr_time = tree.totalTimeAtIndex(t);
    double dt = isVolConst() ? tree.exactTimestepInYears() : tree.timestepAt(t);
    double u = getVol(curr_time) * std::sqrt(dt);

    if (i == 0) {
      double d = -u;
      return tree.nodeValue(t - 1, 0) * std::exp(d);
    }

    return tree.nodeValue(t - 1, i - 1) * std::exp(u);
  }

  double getUpProbAt(double dt, int t, int i) const {
    // The "modeled" probability is 0.5 + 0.5 * (expected_drift_ /
    // annualized_vol_) * std::sqrt(dt); However here for asset pricing we use
    // the risk-neutral:
    double u = annualized_vol_ * std::sqrt(dt);
    double d = -u;
    double r_temp = 0.0;  // TODO add rate
    return (std::exp(r_temp * dt) - std::exp(d)) / (std::exp(u) - std::exp(d));
  }

  void updateVol(double vol) { annualized_vol_ = vol; }

  double expected_drift_;
  double annualized_vol_;
  double spot_price_;
  std::unique_ptr<const TimeDepVolFn> vol_fn_;
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
      double d = expected_drift_ * dt - annualized_vol_ * std::sqrt(dt);
      return tree.nodeValue(t - 1, 0) * std::exp(d);
    } else {
      double u = expected_drift_ * dt + annualized_vol_ * std::sqrt(dt);
      return tree.nodeValue(t - 1, i - 1) * std::exp(u);
    }
  }

  double getUpProbAt(double dt, int t, int i) const {
    // The "modeled" probability is 0.5.
    // However, here for asset pricing we use the risk-neutral:
    double u = expected_drift_ * dt + annualized_vol_ * std::sqrt(dt);
    double d = expected_drift_ * dt - annualized_vol_ * std::sqrt(dt);
    double r_temp = 0.0;  // TODO add rate
    return (std::exp(r_temp * dt) - std::exp(d)) / (std::exp(u) - std::exp(d));
  }

  void updateVol(double vol) { annualized_vol_ = vol; }

  double expected_drift_;
  double annualized_vol_;
  double spot_price_;
};

class AssetTree {};

double call_payoff(double strike, double val) {
  return std::max(0.0, val - strike);
}

double put_payoff(double strike, double val) {
  return std::max(0.0, strike - val);
}

}  // namespace markets

#endif  // MARKETS_BINOMIAL_TREE_H_