#ifndef SMILEEXPLORER_RATES_FIXED_CASHFLOW_INSTRUMENT_H_
#define SMILEEXPLORER_RATES_FIXED_CASHFLOW_INSTRUMENT_H_

#include <vector>

#include "absl/log/log.h"
#include "rates/short_rate_tree_curve.h"
#include "trees/trinomial_tree.h"

namespace smileexplorer {

enum class SwapDirection : int { kPayer = -1, kReceiver = 1 };

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
    const auto& short_rate_tree = short_rate_curve_->trinomialTree();

    // TODO to avoid floating-point error skipping a coupon, compute instead the
    // total number of coupon payments and use that for the loop.
    for (double t = start_date_years; t < end_date_years; t += tenor_in_years) {
      // TODO this is risky, ensure that there is a way to return an error
      // status.
      int reset_time_index =
          bond_tree_.getTimegrid().getTimeIndexForExpiry(t).value();
      int payment_time_index = reset_time_index + timesteps_per_tenor;

      for (auto& node : fwd_rate_tree.tree_[payment_time_index]) {
        // Set all the "final payments" to $1:
        node.state_value = 1.;
      }

      // backward induction back to the payment time.
      for (int ti = payment_time_index - 1; ti >= reset_time_index; --ti) {
        for (int i = 0; i < fwd_rate_tree.numStatesAt(ti); ++i) {
          auto& curr_node = fwd_rate_tree.tree_[ti][i];
          const auto& next_nodes =
              fwd_rate_tree.getSuccessorNodes(curr_node, ti, i);
          double expected_next =
              next_nodes.up.state_value * curr_node.branch_probs.pu +
              next_nodes.mid.state_value * curr_node.branch_probs.pm +
              next_nodes.down.state_value * curr_node.branch_probs.pd;

          double r = short_rate_tree.shortRate(ti, i);
          curr_node.state_value +=
              std::exp(-r * short_rate_tree.getTimegrid().dt(ti)) *
              expected_next;
        }
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

        coupon_induction_tree.tree_[reset_time_index][j].state_value =
            conditional_fwd_rate * principal *
            (-1 * static_cast<int>(direction));
      }

      fwd_rate_tree.clearNodeValues();

      // Now, at the "reset time index" we have state-conditional discount
      // factors at each node. We can turn these into rates and compute the
      // payment on the bond tree.
      for (int ti = reset_time_index; ti < payment_time_index; ++ti) {
        for (int i = 0; i < coupon_induction_tree.numStatesAt(reset_time_index);
             ++i) {
          const auto& curr_node =
              coupon_induction_tree.tree_[reset_time_index][i];
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

      // Now, the probability-weighted cashflows at payment_time_index should be
      // correct. Copy them over!
      for (int i = 0; i < coupon_induction_tree.numStatesAt(payment_time_index);
           ++i) {
        double expected_payment =
            coupon_induction_tree.tree_[payment_time_index][i].state_value;
        bond_tree_.tree_[payment_time_index][i].state_value += expected_payment;
      }
    }
  }

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