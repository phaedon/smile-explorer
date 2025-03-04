#include "derivative.h"

#include "absl/log/log.h"

namespace markets {

double Derivative::getUpProbAt(const BinomialTree& binomial_tree,
                               int t,
                               int i) const {
  const auto& timegrid = binomial_tree.getTimegrid();
  // Hack:
  if (t >= timegrid.size() - 1) {
    t = timegrid.size() - 2;
  }

  // Equation 13.23a (Derman) for the risk-neutral, no-arbitrage up
  // probability.
  double curr = binomial_tree.nodeValue(t, i);
  double up_ratio = binomial_tree.nodeValue(t + 1, i + 1) / curr;
  double down_ratio = binomial_tree.nodeValue(t + 1, i) / curr;
  double df_ratio = 1.0;
  if (curve_.index() != 0) {
    const auto& curve = std::get<1>(curve_);
    df_ratio = curve.df(timegrid.time(t)) / curve.df(timegrid.time(t + 1));
  }
  return (df_ratio - down_ratio) / (up_ratio - down_ratio);
}

void Derivative::backPropagate(const BinomialTree& asset_tree,
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
        t_final, i, payoff_fn(asset_tree.nodeValue(t_final, i)));
  }

  // Back-propagation.
  for (int t = t_final - 1; t >= 0; --t) {
    for (int i = 0; i <= t; ++i) {
      double up = deriv_tree_.nodeValue(t + 1, i + 1);
      double down = deriv_tree_.nodeValue(t + 1, i);
      double up_prob = getUpProbAt(asset_tree, t, i);
      double down_prob = 1 - up_prob;

      double df_ratio = 1.0;
      if (curve_.index() != 0) {
        const auto& timegrid = asset_tree.getTimegrid();
        const auto& curve = std::get<1>(curve_);

        df_ratio = curve.df(timegrid.time(t + 1)) / curve.df(timegrid.time(t));
      }
      deriv_tree_.setValue(t, i, df_ratio * (up * up_prob + down * down_prob));
    }
  }
}

}  // namespace markets
