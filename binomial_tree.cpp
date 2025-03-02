#include "binomial_tree.h"

#include "absl/log/log.h"
namespace markets {

double BinomialTree::getUpProbAt(int t, int i) const {
  // Hack:
  if (t >= timegrid_.size() - 1) {
    t = timegrid_.size() - 2;
  }

  // Equation 13.23a (Derman) for the risk-neutral, no-arbitrage up probability.
  double curr = nodeValue(t, i);
  double up_ratio = nodeValue(t + 1, i + 1) / curr;
  double down_ratio = nodeValue(t + 1, i) / curr;
  double dt = timegrid_.dt(t);
  double r = 0.0;
  double df_ratio = 1.0;
  if (curve_.index() != 0) {
    const auto& curve = std::get<1>(curve_);
    r = std::get<1>(curve_).getForwardRate(timegrid_.time(t),
                                           timegrid_.time(t + 1));

    df_ratio = curve.df(timegrid_.time(t)) / curve.df(timegrid_.time(t + 1));
  }
  return (df_ratio - down_ratio) / (up_ratio - down_ratio);
}

void BinomialTree::backPropagate(const BinomialTree& diffusion,
                                 const std::function<double(double)>& payoff_fn,
                                 double expiry_years) {
  auto t_final_or = timegrid_.getTimeIndexForExpiry(expiry_years);
  if (t_final_or == std::nullopt) {
    LOG(ERROR) << "Backpropagation is impossible for requested expiry "
               << expiry_years;
    return;
  }
  int t_final = t_final_or.value();

  for (int i = t_final + 1; i < tree_.rows(); ++i) {
    tree_.row(i).setZero();
  }

  // Set the payoff at each scenario on the maturity date.
  for (int i = 0; i <= t_final; ++i) {
    setValue(t_final, i, payoff_fn(diffusion.nodeValue(t_final, i)));
  }

  // Back-propagation.
  for (int t = t_final - 1; t >= 0; --t) {
    // std::cout << "At timeindex:" << t << std::endl;
    for (int i = 0; i <= t; ++i) {
      double up = nodeValue(t + 1, i + 1);
      double down = nodeValue(t + 1, i);
      double up_prob = diffusion.getUpProbAt(t, i);
      double down_prob = 1 - up_prob;

      // TODO no discounting (yet)
      setValue(t, i, up * up_prob + down * down_prob);
    }
  }
}

}  // namespace markets