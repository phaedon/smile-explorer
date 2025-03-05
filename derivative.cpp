#include "derivative.h"

#include "absl/log/log.h"

namespace markets {

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

}  // namespace markets
