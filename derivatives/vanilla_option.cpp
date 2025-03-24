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
    case Greeks::Theta:
      return payoff_ == OptionPayoff::Call
                 ? call_theta(spot, strike_, vol, t, r, div) / 365.
                 : put_theta(spot, strike_, vol, t, r, div) / 365.;
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

double VanillaOption::operator()(const TrinomialTree& deriv_tree,
                                 const InterestRateSwap& underlying,
                                 int ti,
                                 int i,
                                 int ti_final) const {
  const auto& bond_tree = underlying.trinomialTree();
  const auto& curr_deriv_node = deriv_tree.tree_[ti][i];

  if (ti == ti_final) {
    // TODO: Also take in a compounding convention to support flexibility in
    // deriv. specs.
    const double state = bond_tree.tree_[ti][i].state_value;
    return getPayoff(state, strike_);
  }

  const auto& next = deriv_tree.getSuccessorNodes(curr_deriv_node, ti, i);
  double expected_next =
      next.up.state_value * curr_deriv_node.branch_probs.pu +
      next.mid.state_value * curr_deriv_node.branch_probs.pm +
      next.down.state_value * curr_deriv_node.branch_probs.pd;

  const auto& short_rate_tree = underlying.shortRateModel().trinomialTree();
  double r = short_rate_tree.shortRate(ti, i);
  const double discounted_expected_next_state =
      std::exp(-r * short_rate_tree.getTimegrid().dt(ti)) * expected_next;

  if (style_ == ExerciseStyle::American) {
    static_assert("Unimplemented!");
  }

  return discounted_expected_next_state;
}

}  // namespace smileexplorer
