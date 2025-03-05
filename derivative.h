#ifndef MARKETS_DERIVATIVE_H_
#define MARKETS_DERIVATIVE_H_

#include "absl/log/log.h"
#include "binomial_tree.h"
#include "markets/rates/rates_curve.h"

namespace markets {

template <typename AssetT>
class Derivative {
 public:
  Derivative(const AssetT* asset, const RatesCurve* curve)
      : deriv_tree_(BinomialTree::createFrom(asset->binomialTree())),
        asset_(asset),
        curve_(curve) {}

  double price(const std::function<double(double)>& payoff_fn,
               double expiry_years) {
    backPropagate(payoff_fn, expiry_years);
    return deriv_tree_.nodeValue(0, 0);
  }

  const BinomialTree& binomialTree() const { return deriv_tree_; }

  // void update(const BinomialTree& updated_tree) {
  //  deriv_tree_ = BinomialTree::createFrom(updated_tree);
  // }

 private:
  BinomialTree deriv_tree_;

  const AssetT* asset_;
  const RatesCurve* curve_;

  void backPropagate(const std::function<double(double)>& payoff_fn,
                     double expiry_years) {
    const auto& asset_tree = asset_->binomialTree();
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
          t_final, i, payoff_fn(asset_tree.nodeValue(t_final, i)));
    }

    // Back-propagation.
    for (int t = t_final - 1; t >= 0; --t) {
      for (int i = 0; i <= t; ++i) {
        double up = deriv_tree_.nodeValue(t + 1, i + 1);
        double down = deriv_tree_.nodeValue(t + 1, i);
        double up_prob = asset_tree.getUpProbAt(*curve_, t, i);
        double down_prob = 1 - up_prob;

        const auto& timegrid = asset_tree.getTimegrid();
        double inv_fwd_df =
            curve_->forwardDF(timegrid.time(t + 1), timegrid.time(t));

        deriv_tree_.setValue(
            t, i, inv_fwd_df * (up * up_prob + down * down_prob));
      }
    }
  }
};

}  // namespace markets

#endif  // MARKETS_DERIVATIVE_H_