#include "trees/binomial_tree.h"

#include "absl/log/log.h"
#include "rates/rates_curve.h"

namespace markets {

double BinomialTree::getUpProbAt(
    const RatesCurve& curve,
    int t,
    int i,
    const std::optional<const RatesCurve*> foreign_curve) const {
  const auto& timegrid = getTimegrid();

  double curr = nodeValue(t, i);
  double up_ratio = nodeValue(t + 1, i + 1) / curr;
  double down_ratio = nodeValue(t + 1, i) / curr;

  double inv_forward_df =
      curve.inverseForwardDF(timegrid.time(t), timegrid.time(t + 1));

  if (foreign_curve.has_value()) {
    double foreign_inv_forward_df = foreign_curve.value()->inverseForwardDF(
        timegrid.time(t), timegrid.time(t + 1));
    inv_forward_df /= foreign_inv_forward_df;
  }

  double risk_neutral_up_prob =
      (inv_forward_df - down_ratio) / (up_ratio - down_ratio);

  if (risk_neutral_up_prob <= 0.0 || risk_neutral_up_prob >= 1.0) {
    LOG(WARNING) << "No-arbitrage condition violated as risk-neutral up-prob "
                    "is outside the range (0,1).";
  }

  return risk_neutral_up_prob;
}

double BinomialTree::getUpProbAt(const RatesCurve& curve, int t, int i) const {
  return getUpProbAt(curve, t, i, std::nullopt);
}

double BinomialTree::getUpProbAt(const RatesCurve& domestic_curve,
                                 const RatesCurve& foreign_curve,
                                 int t,
                                 int i) const {
  return getUpProbAt(domestic_curve, t, i, &foreign_curve);
}

}  // namespace markets