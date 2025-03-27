#ifndef SMILEEXPLORER_RATES_FIXED_CASHFLOW_INSTRUMENT_H_
#define SMILEEXPLORER_RATES_FIXED_CASHFLOW_INSTRUMENT_H_

#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "rates/short_rate_tree_curve.h"
#include "trees/trinomial_tree.h"
#include "trees/trinomial_tree_operators.h"

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

  absl::Status setCashflows(const std::vector<Cashflow>& cashflows) {
    bond_tree_.clearNodeValues();
    int num_failed = 0;
    for (const auto& cashflow : cashflows) {
      if (auto curr_status = addCashflowToTree(cashflow);
          curr_status != absl::OkStatus()) {
        ++num_failed;
        LOG(ERROR) << "Cashflow failed: " << curr_status.message();
      }
    }
    if (num_failed > 0) {
      return absl::InvalidArgumentError(absl::StrCat(
          num_failed, " of ", cashflows.size(), " total cashflows failed."));
    }
    return absl::OkStatus();
  }

  absl::Status addCashflowToTree(const Cashflow& cashflow) {
    auto time_index =
        short_rate_curve_->trinomialTree().getTimegrid().getTimeIndexForExpiry(
            cashflow.time_years);
    if (time_index == std::nullopt) {
      return absl::InvalidArgumentError(
          absl::StrCat("Cannot add cashflow at t=",
                       cashflow.time_years,
                       " beyond tree maturity."));
    }

    for (auto& node : bond_tree_.tree_[time_index.value()]) {
      node.state_value += cashflow.amount;
    }

    return absl::OkStatus();
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