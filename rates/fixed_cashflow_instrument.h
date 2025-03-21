#ifndef SMILEEXPLORER_RATES_FIXED_CASHFLOW_INSTRUMENT_H_
#define SMILEEXPLORER_RATES_FIXED_CASHFLOW_INSTRUMENT_H_

#include <vector>

#include "absl/log/log.h"
#include "rates/short_rate_tree_curve.h"
#include "trees/trinomial_tree.h"

namespace smileexplorer {

struct Cashflow {
  double time_years;
  double amount;
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

  // TODO: This is essentially backward-induction (yet again) and we should
  // therefore abstract it out, in combination with InterestRateDerivative once
  // the common functionality is completely obvious.
  double price() {
    // Subtract 2 because we need to start the backward induction one step
    // before the end of the timegrid (since we always look one step forward).
    const auto& short_rate_tree = short_rate_curve_->trinomialTree();

    const int final_time_index = bond_tree_.tree_.size() - 2;
    for (int ti = final_time_index; ti >= 0; --ti) {
      for (int i = 0; i < bond_tree_.numStatesAt(ti); ++i) {
        auto& curr_bond_node = bond_tree_.tree_[ti][i];
        const auto& next_bond_nodes =
            bond_tree_.getSuccessorNodes(curr_bond_node, ti, i);

        double expected_next =
            next_bond_nodes.up.state_value * curr_bond_node.branch_probs.pu +
            next_bond_nodes.mid.state_value * curr_bond_node.branch_probs.pm +
            next_bond_nodes.down.state_value * curr_bond_node.branch_probs.pd;

        double r = short_rate_tree.shortRate(ti, i);

        curr_bond_node.state_value +=
            std::exp(-r * short_rate_tree.getTimegrid().dt(ti)) * expected_next;
      }
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