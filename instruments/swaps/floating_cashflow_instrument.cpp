#include "instruments/swaps/floating_cashflow_instrument.h"

#include "contract.h"

namespace smileexplorer {
namespace {
void propagateCouponAmountsForward(TrinomialTree& coupon_tree,
                                   int reset_time_index,
                                   int payment_time_index) {
  // Now, at the "reset time index" we have state-conditional discount
  // factors at each node. We can turn these into rates and compute the
  // payment on the bond tree.
  for (int ti = reset_time_index; ti < payment_time_index; ++ti) {
    for (int i = 0; i < coupon_tree.numStatesAt(ti); ++i) {
      const auto& curr_node = coupon_tree.tree_[ti][i];

      NodeTriplet<TrinomialNode> next_nodes =
          coupon_tree.getSuccessorNodes(curr_node, ti, i);

      // Propagate forward *without* discounting.
      next_nodes.up.state_value +=
          curr_node.state_value * curr_node.branch_probs.pu;
      next_nodes.mid.state_value +=
          curr_node.state_value * curr_node.branch_probs.pm;
      next_nodes.down.state_value +=
          curr_node.state_value * curr_node.branch_probs.pd;
    }
  }
}

void scaleExpectedCouponsByCumulativeProbabilities(TrinomialTree& coupon_tree,
                                                   int payment_time_index) {
  double arrow_deb_sum = 0.;
  for (auto& pmt_node : coupon_tree.tree_[payment_time_index]) {
    arrow_deb_sum += pmt_node.arrow_debreu;
  }

  for (auto& pmt_node : coupon_tree.tree_[payment_time_index]) {
    pmt_node.state_value /= (pmt_node.arrow_debreu / arrow_deb_sum);
  }
}

TrinomialTree calculateConditionalCoupons(const TrinomialTree& fwd_rate_tree,
                                          SwapContractDetails contract,
                                          int reset_time_index) {
  auto coupon_induction_tree = TrinomialTree::createFrom(fwd_rate_tree);
  // Initialise the "coupon forward-induction tree" at the reset time.
  for (int j = 0; j < fwd_rate_tree.numStatesAt(reset_time_index); ++j) {
    // This is already scaled for the length of the tenor (i.e. we don't
    // try to annualise it). Therefore it can be used to compute the coupon
    // payment directly.
    double conditional_fwd_rate =
        1. / fwd_rate_tree.tree_[reset_time_index][j].state_value - 1;

    const double coupon_amount = conditional_fwd_rate *
                                 contract.notional_principal *
                                 (-1 * static_cast<int>(contract.direction));
    coupon_induction_tree.setProbabilityWeightedNodeValue(
        reset_time_index, j, coupon_amount);
  }
  return coupon_induction_tree;
}
}  // namespace

void FloatingCashflowInstrument::setCashflows(SwapContractDetails contract) {
  auto fwd_rate_tree =
      TrinomialTree::createFrom(short_rate_curve_->trinomialTree());

  const int timesteps_per_tenor =
      bond_tree_.timestepsPerForwardRateTenor(contract.floating_rate_frequency);

  // Only full cashflows are supported. (Partial period cashflows, such as a
  // 1y1m swap with quarterly payments, are not supported here.)
  const int cashflows_per_year =
      std::round(kNumMonthsPerYear /
                 static_cast<double>(contract.floating_rate_frequency));
  const int num_cashflows =
      (contract.end_date_years - contract.start_date_years) *
      cashflows_per_year;

  for (int cashflow_index = 0; cashflow_index < num_cashflows;
       ++cashflow_index) {
    double reset_time =
        static_cast<double>(cashflow_index) / cashflows_per_year +
        contract.start_date_years;

    // TODO this is risky, ensure that there is a way to return an error
    // status.
    int reset_time_index =
        bond_tree_.getTimegrid().getTimeIndexForExpiry(reset_time).value();

    int payment_time_index = reset_time_index + timesteps_per_tenor;

    // Set all the "final payments" to $1:
    fwd_rate_tree.setNodeValuesAtTimeIndex(payment_time_index, 1.0);

    // Backward induction back to the payment time.
    auto status = runBackwardInduction(*short_rate_curve_,
                                       fwd_rate_tree,
                                       payment_time_index,
                                       reset_time_index);
    if (!status.ok()) {
      LOG(ERROR) << status.message();
      return;
    }

    auto coupon_induction_tree =
        calculateConditionalCoupons(fwd_rate_tree, contract, reset_time_index);

    fwd_rate_tree.clearNodeValues();

    propagateCouponAmountsForward(
        coupon_induction_tree, reset_time_index, payment_time_index);

    scaleExpectedCouponsByCumulativeProbabilities(coupon_induction_tree,
                                                  payment_time_index);

    // Now, the probability-weighted cashflows at payment_time_index should be
    // correct. Copy them over! (Note: this may need to be a += or merge
    // operation)
    bond_tree_.copyNodeValuesAtTimeIndex(payment_time_index,
                                         coupon_induction_tree);
  }
}

}  // namespace smileexplorer