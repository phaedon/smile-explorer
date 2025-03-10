#include "vanilla_option.h"

#include "absl/log/log.h"
#include "bsm.h"

namespace smileexplorer {

double VanillaOption::blackScholes(
    double spot, double vol, double t, double r, double div) const {
  if (style_ == ExerciseStyle::American) {
    LOG(ERROR) << "No closed-form solution for American options.";
    return 0.0;
  }
  if (payoff_ == OptionPayoff::Call) {
    return call(spot, strike_, vol, t, r, div);
  } else {
    return put(spot, strike_, vol, t, r, div);
  }
}

double VanillaOption::blackScholesGreek(double spot,
                                        double vol,
                                        double t,
                                        double r,
                                        double div,
                                        Greeks greek) const {
  if (style_ == ExerciseStyle::American) {
    LOG(ERROR) << "No closed-form solution for American options.";
    return 0.0;
  }
  switch (greek) {
    case Greeks::Delta:
      return payoff_ == OptionPayoff::Call
                 ? call_delta(spot, strike_, vol, t, r, div)
                 : put_delta(spot, strike_, vol, t, r, div);
    case Greeks::Vega:
      return vega(spot, strike_, vol, t, r, div);
    case Greeks::Gamma:
      return gamma(spot, strike_, vol, t, r, div);
    default:
      return 0.0;
  }
}

double VanillaOption::operator()(const BinomialTree& deriv_tree,
                                 const BinomialTree& asset_tree,
                                 int ti,
                                 int i,
                                 int ti_final,
                                 double up_prob,
                                 double fwd_df) const {
  if (ti == ti_final) {
    const double state = asset_tree.nodeValue(ti, i);
    return getPayoff(state, strike_);
  }

  const double up = deriv_tree.nodeValue(ti + 1, i + 1);
  const double down = deriv_tree.nodeValue(ti + 1, i);
  const double down_prob = 1 - up_prob;

  const double discounted_expected_next_state =
      fwd_df * (up * up_prob + down * down_prob);

  if (style_ == ExerciseStyle::American) {
    const double state = asset_tree.nodeValue(ti, i);
    const double payoff_if_early_exercise = getPayoff(state, strike_);
    return std::max(payoff_if_early_exercise, discounted_expected_next_state);
  }

  return discounted_expected_next_state;
}

}  // namespace smileexplorer
