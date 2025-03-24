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

  void setCashflows(SwapContractDetails contract);

  double price() {
    auto status = runBackwardInduction(
        *short_rate_curve_, bond_tree_, bond_tree_.tree_.size() - 1, 0);
    if (!status.ok()) {
      LOG(ERROR) << status.message();
      return 0.0;
    }

    return bond_tree_.tree_[0][0].state_value;
  }

  const TrinomialTree& trinomialTree() const { return bond_tree_; }

 private:
  const ShortRateTreeCurve* short_rate_curve_;

  // We use "bond" loosely here to denote any instrument with a set of floating
  // future cashflows.
  TrinomialTree bond_tree_;
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_RATES_FLOATING_CASHFLOW_INSTRUMENT_H_