#include "trees/binomial_tree.h"

#include "absl/log/log.h"
#include "rates/rates_curve.h"

namespace markets {

// double BinomialTree::getUpProbAt(const RatesCurve& domestic_curve,
//                                  const RatesCurve& foreign_curve,
//                                  int t,
//                                  int i) const {
//   const auto& timegrid = getTimegrid();

//   double curr = nodeValue(t, i);
//   double up_ratio = nodeValue(t + 1, i + 1) / curr;
//   double down_ratio = nodeValue(t + 1, i) / curr;
//   double domestic_inv_forward_df =
//       domestic_curve.inverseForwardDF(timegrid.time(t), timegrid.time(t +
//       1));
//   double foreign_inv_forward_df =
//       foreign_curve.inverseForwardDF(timegrid.time(t), timegrid.time(t + 1));
//   double inv_forward_df = domestic_inv_forward_df / foreign_inv_forward_df;

//   double risk_neutral_up_prob =
//       (inv_forward_df - down_ratio) / (up_ratio - down_ratio);
//   if (risk_neutral_up_prob <= 0.0 || risk_neutral_up_prob >= 1.0) {
//     LOG(WARNING)
//         << "No-arbitrage condition violated as risk-neutral up-prob is "
//            "outside the range (0,1).";
//   }

//   // return std::clamp(risk_neutral_up_prob, 0., 1.);
//   return risk_neutral_up_prob;
// }

double BinomialTree::getUpProbAt(const RatesCurve& curve, int t, int i) const {
  const auto& timegrid = getTimegrid();
  // Hack:
  if (t >= timegrid.size() - 1) {
    t = timegrid.size() - 2;
  }

  // Equation 13.23a (Derman) for the risk-neutral, no-arbitrage up
  // probability.
  double curr = nodeValue(t, i);
  double up_ratio = nodeValue(t + 1, i + 1) / curr;
  double down_ratio = nodeValue(t + 1, i) / curr;
  double forward_df =
      curve.inverseForwardDF(timegrid.time(t), timegrid.time(t + 1));

  double risk_neutral_up_prob =
      (forward_df - down_ratio) / (up_ratio - down_ratio);
  if (risk_neutral_up_prob <= 0.0 || risk_neutral_up_prob >= 1.0) {
    LOG(ERROR) << "No-arbitrage condition violated as risk-neutral up-prob is "
                  "outside the range (0,1).";
  }

  return std::clamp(risk_neutral_up_prob, 0., 1.);
  return risk_neutral_up_prob;
}

}  // namespace markets