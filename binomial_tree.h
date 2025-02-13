#include <Eigen/Dense>
#include <chrono>
#include <cmath>
#include <iostream>

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
  BinomialTree(std::chrono::years total_duration, std::chrono::weeks timestep) {
    int num_timesteps = total_duration / timestep + 1;
    tree_.resize(num_timesteps, num_timesteps);
  }

  BinomialTree(std::chrono::months total_duration, std::chrono::days timestep) {
    int num_timesteps = total_duration / timestep + 1;
    tree_.resize(num_timesteps, num_timesteps);
  }

  double numTimesteps() const { return tree_.rows(); }

  void setInitValue(double val) { setValue(0, 0, val); }

  template <typename UpFn, typename DownFn>
  void populateTreeForward(UpFn up_fn, DownFn down_fn) {
    for (int t = 0; t < numTimesteps() - 1; t++) {
      for (int i = 0; i <= t; ++i) {
        double curr_val = tree_(t, i);
        setValue(t + 1, i + 1, up_fn(curr_val));
        setValue(t + 1, i, down_fn(curr_val));
      }
    }
  }

  void print() const { std::cout << tree_; }

  double nodeValue(int time, int node_index) const {
    return tree_(time, node_index);
  }

 private:
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> tree_;

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

}  // namespace markets