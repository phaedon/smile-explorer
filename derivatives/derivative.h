#ifndef MARKETS_DERIVATIVE_H_
#define MARKETS_DERIVATIVE_H_

#include "absl/log/log.h"
#include "rates/rates_curve.h"
#include "trees/binomial_tree.h"

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
    runBackwardInduction(payoff_fn, expiry_years);
    return deriv_tree_.nodeValue(0, 0);
  }

  const BinomialTree& binomialTree() const { return deriv_tree_; }

 private:
  BinomialTree deriv_tree_;

 protected:
  const AssetT* asset_;
  const RatesCurve* curve_;

 private:
  virtual double getUpProbAt(int time_index, int i) const {
    return asset_->binomialTree().getUpProbAt(*curve_, time_index, i);
  }

  double forwardDF(int t) const {
    const auto& timegrid = asset_->binomialTree().getTimegrid();
    return curve_->forwardDF(timegrid.time(t), timegrid.time(t + 1));
  }

  void runBackwardInduction(const std::function<double(double)>& payoff_fn,
                            double expiry_years) {
    const auto& asset_tree = asset_->binomialTree();
    auto t_final_or =
        deriv_tree_.getTimegrid().getTimeIndexForExpiry(expiry_years);
    if (t_final_or == std::nullopt) {
      LOG(ERROR) << "Backward induction is impossible for requested expiry "
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

    // Backward induction.
    for (int t = t_final - 1; t >= 0; --t) {
      for (int i = 0; i <= t; ++i) {
        double up = deriv_tree_.nodeValue(t + 1, i + 1);
        double down = deriv_tree_.nodeValue(t + 1, i);
        const double up_prob = getUpProbAt(t, i);
        const double down_prob = 1 - up_prob;
        deriv_tree_.setValue(
            t, i, forwardDF(t) * (up * up_prob + down * down_prob));
      }
    }
  }
};

template <typename AssetT>
class CurrencyDerivative : public Derivative<AssetT> {
 public:
  CurrencyDerivative(const AssetT* asset,
                     const RatesCurve* domestic_curve,
                     const RatesCurve* foreign_curve)
      : Derivative<AssetT>(asset, domestic_curve),
        foreign_curve_(foreign_curve) {}

  double getUpProbAt(int time_index, int i) const override {
    return this->asset_->binomialTree().getUpProbAt(
        *this->curve_, *foreign_curve_, time_index, i);
  }

 private:
  const RatesCurve* foreign_curve_;
};

}  // namespace markets

#endif  // MARKETS_DERIVATIVE_H_