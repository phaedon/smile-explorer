#ifndef SMILEEXPLORER_RATES_FIXED_CASHFLOW_INSTRUMENT_H_
#define SMILEEXPLORER_RATES_FIXED_CASHFLOW_INSTRUMENT_H_

#include <vector>

#include "absl/log/log.h"
#include "instruments/swaps/contract.h"
#include "rates/short_rate_tree_curve.h"
#include "trees/trinomial_tree.h"
#include "trees/trinomial_tree_operators.h"

namespace smileexplorer {

struct Cashflow {
  double time_years;
  double amount;
};

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
    double tenor_in_years = timesteps_per_tenor * bond_tree_.dt_;

    // Only full cashflows are supported. (Partial period cashflows, such as a
    // 1y1m swap with quarterly payments, are not supported here.)
    const int cashflows_per_year =
        std::round(kNumMonthsPerYear / static_cast<double>(tenor));
    const int num_cashflows =
        (end_date_years - start_date_years) * cashflows_per_year;

    for (int cashflow_index = 0; cashflow_index < num_cashflows;
         ++cashflow_index) {
      double reset_time =
          cashflow_index * cashflows_per_year + start_date_years;
      std::cout << "reset_time: " << reset_time << std::endl;

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
      std::cout << " Value of dollar at start: "
                << fwd_rate_tree.nodeValue(0, 0) << std::endl;
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
        std::cout << "Conditional fwd rate at reset time: "
                  << conditional_fwd_rate << std::endl;

        coupon_induction_tree.tree_[reset_time_index][j].state_value =
            conditional_fwd_rate * principal *
            (-1 * static_cast<int>(direction));

        std::cout << "Coupon computed at " << reset_time << " == "
                  << coupon_induction_tree.nodeValue(reset_time_index, j)
                  << std::endl;
      }

      fwd_rate_tree.clearNodeValues();

      // Now, at the "reset time index" we have state-conditional discount
      // factors at each node. We can turn these into rates and compute the
      // payment on the bond tree.
      for (int ti = reset_time_index; ti < payment_time_index; ++ti) {
        for (int i = 0; i < coupon_induction_tree.numStatesAt(ti); ++i) {
          const auto& curr_node = coupon_induction_tree.tree_[ti][i];

          std::cout << "Curr node coupon @ ti=" << ti << " i=" << i
                    << "  == " << curr_node.state_value << std::endl;

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
      std::cout << " ad sum at payment_time_index =" << payment_time_index
                << "   === " << ad_sum << std::endl;
      for (auto& pmt_node : coupon_induction_tree.tree_[payment_time_index]) {
        std::cout << "   ... before scaling: Payment node coupon @ state == "
                  << pmt_node.state_value
                  << " with arrow_deb@node=" << pmt_node.arrow_debreu
                  << std::endl;
        pmt_node.state_value /= (pmt_node.arrow_debreu / ad_sum);

        std::cout << "Payment node coupon @ state == " << pmt_node.state_value
                  << std::endl;
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

    std::cout << "Floating leg value: " << bond_tree_.tree_[0][0].state_value
              << std::endl;

    return bond_tree_.tree_[0][0].state_value;
  }

  const TrinomialTree& trinomialTree() const { return bond_tree_; }

 private:
  const ShortRateTreeCurve* short_rate_curve_;

  // We use "bond" loosely here to denote any instrument with a set of fixed
  // future cashflows.
  TrinomialTree bond_tree_;
};

/**
 * @brief Represents any fixed-income instrument (such as a bond or a FRA)
 * with one or more fixed cashflows.
 *
 */
class FixedCashflowInstrument {
 public:
  FixedCashflowInstrument(const ShortRateTreeCurve* short_rate_curve)
      : short_rate_curve_(short_rate_curve),
        bond_tree_(
            TrinomialTree::createFrom(short_rate_curve->trinomialTree())) {}

  void setCashflows(const std::vector<Cashflow>& cashflows) {
    bond_tree_.clearNodeValues();
    for (const auto& cashflow : cashflows) {
      addCashflowToTree(cashflow);
    }
  }

  void addCashflowToTree(const Cashflow& cashflow) {
    auto time_index =
        short_rate_curve_->trinomialTree().getTimegrid().getTimeIndexForExpiry(
            cashflow.time_years);
    if (time_index == std::nullopt) {
      LOG(ERROR) << "Cannot add cashflow at t=" << cashflow.time_years
                 << " on tree which is only "
                 << short_rate_curve_->trinomialTree().tree_.size() *
                        short_rate_curve_->trinomialTree().dt_;
      return;
    }

    for (auto& node : bond_tree_.tree_[time_index.value()]) {
      node.state_value += cashflow.amount;
    }
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

  const ShortRateTreeCurve& shortRateModel() const {
    return *short_rate_curve_;
  }

 private:
  const ShortRateTreeCurve* short_rate_curve_;

  // We use "bond" loosely here to denote any instrument with a set of fixed
  // future cashflows.
  TrinomialTree bond_tree_;
};

}  // namespace smileexplorer
#endif  // SMILEEXPLORER_RATES_FIXED_CASHFLOW_INSTRUMENT_H_