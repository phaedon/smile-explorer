#ifndef SMILEEXPLORER_RATES_FLOATING_CASHFLOW_INSTRUMENT_H_
#define SMILEEXPLORER_RATES_FLOATING_CASHFLOW_INSTRUMENT_H_

#include "instruments/swaps/contract.h"
#include "rates/short_rate_tree_curve.h"
#include "trees/trinomial_tree.h"
#include "trees/trinomial_tree_operators.h"

namespace smileexplorer {

class FloatingCashflowInstrument {
 public:
  FloatingCashflowInstrument(const ShortRateTreeCurve* short_rate_curve)
      : short_rate_curve_(short_rate_curve),
        bond_tree_(
            TrinomialTree::createFrom(short_rate_curve->trinomialTree())) {}

  void setCashflows(double principal,
                    ForwardRateTenor tenor,
                    SwapDirection direction,
                    double start_date_years,
                    double end_date_years) {
    auto fwd_rate_tree =
        TrinomialTree::createFrom(short_rate_curve_->trinomialTree());

    int timesteps_per_tenor = bond_tree_.timestepsPerForwardRateTenor(tenor);

    // Only full cashflows are supported. (Partial period cashflows, such as a
    // 1y1m swap with quarterly payments, are not supported here.)
    const int cashflows_per_year =
        std::round(kNumMonthsPerYear / static_cast<double>(tenor));
    const int num_cashflows =
        (end_date_years - start_date_years) * cashflows_per_year;

    for (int cashflow_index = 0; cashflow_index < num_cashflows;
         ++cashflow_index) {
      double reset_time =
          static_cast<double>(cashflow_index) / cashflows_per_year +
          start_date_years;

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
          TrinomialTree::createFrom(short_rate_curve_->trinomialTree());
      // Initialise the "coupon forward-induction tree" at the reset time.
      for (int j = 0; j < bond_tree_.numStatesAt(reset_time_index); ++j) {
        // This is already scaled for the length of the tenor (i.e. we don't
        // try to annualise it). Therefore it can be used to compute the coupon
        // payment directly.
        double conditional_fwd_rate =
            1. / fwd_rate_tree.tree_[reset_time_index][j].state_value - 1;

        const double coupon_amount = conditional_fwd_rate * principal *
                                     (-1 * static_cast<int>(direction));
        coupon_induction_tree.setProbabilityWeightedNodeValue(
            reset_time_index, j, coupon_amount);
      }

      fwd_rate_tree.clearNodeValues();

      // Now, at the "reset time index" we have state-conditional discount
      // factors at each node. We can turn these into rates and compute the
      // payment on the bond tree.
      for (int ti = reset_time_index; ti < payment_time_index; ++ti) {
        for (int i = 0; i < coupon_induction_tree.numStatesAt(ti); ++i) {
          const auto& curr_node = coupon_induction_tree.tree_[ti][i];

          NodeTriplet<TrinomialNode> next_nodes =
              coupon_induction_tree.getSuccessorNodes(curr_node, ti, i);

          // Propagate forward *without* discounting.
          next_nodes.up.state_value +=
              curr_node.state_value * curr_node.branch_probs.pu;
          next_nodes.mid.state_value +=
              curr_node.state_value * curr_node.branch_probs.pm;
          next_nodes.down.state_value +=
              curr_node.state_value * curr_node.branch_probs.pd;
        }
      }

      double ad_sum = 0.;
      for (auto& pmt_node : coupon_induction_tree.tree_[payment_time_index]) {
        ad_sum += pmt_node.arrow_debreu;
      }

      for (auto& pmt_node : coupon_induction_tree.tree_[payment_time_index]) {
        pmt_node.state_value /= (pmt_node.arrow_debreu / ad_sum);
      }

      // Now, the probability-weighted cashflows at payment_time_index should be
      // correct. Copy them over! (Note: this may need to be a += or merge
      // operation)
      bond_tree_.copyNodeValuesAtTimeIndex(payment_time_index,
                                           coupon_induction_tree);
    }
  }

  const ShortRateTreeCurve& shortRateModel() const {
    return *short_rate_curve_;
  }

  double price() {
    auto status = runBackwardInduction(
        shortRateModel(), bond_tree_, bond_tree_.tree_.size() - 1, 0);
    if (!status.ok()) {
      LOG(ERROR) << status.message();
      return 0.0;
    }

    return bond_tree_.tree_[0][0].state_value;
  }

  const TrinomialTree& trinomialTree() const { return bond_tree_; }

 private:
  const ShortRateTreeCurve* short_rate_curve_;

  // We use "bond" loosely here to denote any instrument with a set of fixed
  // future cashflows.
  TrinomialTree bond_tree_;
};

}  // namespace smileexplorer
#endif  // SMILEEXPLORER_RATES_FLOATING_CASHFLOW_INSTRUMENT_H_