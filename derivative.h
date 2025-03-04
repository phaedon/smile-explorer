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
    backPropagate(asset.binomialTree(), payoff_fn, expiry_years);
    return deriv_tree_.nodeValue(0, 0);
  }

  // Returns the risk-neutral, no-arbitrage up-probability at time index t and
  // state i. Because this requires discounting, we'll use this only in
  // backprop.
  // Exposed for testing.
  double getUpProbAt(const BinomialTree& binomial_tree, int t, int i) const;

  const BinomialTree& binomialTree() const { return deriv_tree_; }

  void update(const BinomialTree& updated_tree) {
    deriv_tree_ = BinomialTree::createFrom(updated_tree);
  }

 private:
  BinomialTree deriv_tree_;
  RatesCurve curve_;

  void backPropagate(const BinomialTree& asset_tree,
                     const std::function<double(double)>& payoff_fn,
                     double expiry_years);
};

}  // namespace markets

#endif  // MARKETS_DERIVATIVE_H_