#ifndef MARKETS_DERIVATIVE_H_
#define MARKETS_DERIVATIVE_H_

#include "absl/log/log.h"
#include "binomial_tree.h"
#include "markets/rates/rates_curve.h"

namespace markets {

class Derivative {
 public:
  Derivative(const BinomialTree& underlying_tree)
      : deriv_tree_(BinomialTree::createFrom(underlying_tree)) {}

  Derivative(const BinomialTree& underlying_tree, const RatesCurve& curve)
      : deriv_tree_(BinomialTree::createFrom(underlying_tree)), curve_(curve) {}

  template <typename AssetT>
  double price(const AssetT& asset,
               const std::function<double(double)>& payoff_fn,
               double expiry_years) {
    backPropagate(asset, payoff_fn, expiry_years);
    return deriv_tree_.nodeValue(0, 0);
  }

  // Returns the risk-neutral, no-arbitrage up-probability at time index t and
  // state i. Because this requires discounting, we'll use this only in
  // backprop.
  template <typename AssetT>
  double getUpProbAt(const AssetT& asset, int t, int i) const {
    const auto& timegrid = asset.binomialTree().getTimegrid();
    // Hack:
    if (t >= timegrid.size() - 1) {
      t = timegrid.size() - 2;
    }

    // Equation 13.23a (Derman) for the risk-neutral, no-arbitrage up
    // probability.
    double curr = asset.binomialTree().nodeValue(t, i);
    double up_ratio = asset.binomialTree().nodeValue(t + 1, i + 1) / curr;
    double down_ratio = asset.binomialTree().nodeValue(t + 1, i) / curr;
    double dt = timegrid.dt(t);
    double r = 0.0;
    double df_ratio = 1.0;
    if (curve_.index() != 0) {
      const auto& curve = std::get<1>(curve_);
      r = std::get<1>(curve_).getForwardRate(timegrid.time(t),
                                             timegrid.time(t + 1));

      df_ratio = curve.df(timegrid.time(t)) / curve.df(timegrid.time(t + 1));
    }
    return (df_ratio - down_ratio) / (up_ratio - down_ratio);
  }

  template <typename AssetT>
  void backPropagate(const AssetT& asset,
                     const std::function<double(double)>& payoff_fn,
                     double expiry_years) {
    auto t_final_or =
        deriv_tree_.getTimegrid().getTimeIndexForExpiry(expiry_years);
    if (t_final_or == std::nullopt) {
      LOG(ERROR) << "Backpropagation is impossible for requested expiry "
                 << expiry_years;
      return;
    }
    int t_final = t_final_or.value();
    deriv_tree_.setZeroAfterIndex(t_final);

    // Set the payoff at each scenario on the maturity date.
    for (int i = 0; i <= t_final; ++i) {
      deriv_tree_.setValue(
          t_final, i, payoff_fn(asset.binomialTree().nodeValue(t_final, i)));
    }

    // Back-propagation.
    for (int t = t_final - 1; t >= 0; --t) {
      // std::cout << "At timeindex:" << t << std::endl;
      for (int i = 0; i <= t; ++i) {
        double up = deriv_tree_.nodeValue(t + 1, i + 1);
        double down = deriv_tree_.nodeValue(t + 1, i);
        double up_prob = getUpProbAt(asset, t, i);
        double down_prob = 1 - up_prob;

        double df_ratio = 1.0;
        if (curve_.index() != 0) {
          const auto& timegrid = asset.binomialTree().getTimegrid();
          const auto& curve = std::get<1>(curve_);

          df_ratio =
              curve.df(timegrid.time(t + 1)) / curve.df(timegrid.time(t));
        }
        deriv_tree_.setValue(
            t, i, df_ratio * (up * up_prob + down * down_prob));
      }
    }
  }

  const BinomialTree& binomialTree() const { return deriv_tree_; }

  void update(const BinomialTree& updated_tree) {
    deriv_tree_ = BinomialTree::createFrom(updated_tree);
  }

 private:
  BinomialTree deriv_tree_;
  RatesCurve curve_;
};

}  // namespace markets

#endif  // MARKETS_DERIVATIVE_H_