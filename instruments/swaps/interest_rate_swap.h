#ifndef SMILEEXPLORER_INSTRUMENTS_SWAPS_INTEREST_RATE_SWAP_H_
#define SMILEEXPLORER_INSTRUMENTS_SWAPS_INTEREST_RATE_SWAP_H_

#include "contract.h"
#include "instruments/swaps/fixed_cashflow_instrument.h"
#include "instruments/swaps/floating_cashflow_instrument.h"
#include "rates/short_rate_tree_curve.h"

namespace smileexplorer {
class InterestRateSwap {
 public:
  InterestRateSwap(FixedCashflowInstrument fixed_leg,
                   FloatingCashflowInstrument floating_leg)
      : fixed_leg_(std::move(fixed_leg)),
        floating_leg_(std::move(floating_leg)),
        combined_tree_(TrinomialTree::createFrom(fixed_leg.trinomialTree())) {
    mergeTrees();
  }

  static InterestRateSwap createFromContract(SwapContractDetails contract,
                                             const ShortRateTreeCurve* curve) {
    FixedCashflowInstrument fixed_leg(curve);
    FloatingCashflowInstrument floating_leg(curve);

    double fixed_rate_tenor =
        1. / static_cast<int>(contract.fixed_rate_frequency);
    for (double t = contract.start_date_years + fixed_rate_tenor;
         t <= contract.end_date_years;
         t += fixed_rate_tenor) {
      Cashflow cashflow{
          .time_years = t,
          .amount = contract.fixed_rate * contract.notional_principal *
                    fixed_rate_tenor * static_cast<int>(contract.direction)};
      fixed_leg.addCashflowToTree(cashflow);
    }

    floating_leg.setCashflows(contract.notional_principal,
                              contract.floating_rate_frequency,
                              contract.direction,
                              contract.start_date_years,
                              contract.end_date_years);

    return InterestRateSwap(std::move(fixed_leg), std::move(floating_leg));
  }

  static InterestRateSwap createBond(FixedCashflowInstrument fixed_leg) {
    // Create an empty placeholder floating leg.
    FloatingCashflowInstrument floating_leg(&fixed_leg.shortRateModel());
    return InterestRateSwap(std::move(fixed_leg), std::move(floating_leg));
  }

  double price() { return fixed_leg_.price() + floating_leg_.price(); }

  const TrinomialTree& trinomialTree() const { return combined_tree_; }

  const ShortRateTreeCurve& shortRateModel() const {
    return fixed_leg_.shortRateModel();
  }

 private:
  // SwapContractDetails contract_;
  // const ShortRateTreeCurve* curve_;
  FixedCashflowInstrument fixed_leg_;
  FloatingCashflowInstrument floating_leg_;

  TrinomialTree combined_tree_;

  void mergeTrees() {
    combined_tree_ = fixed_leg_.trinomialTree();
    for (size_t ti = 0; ti < combined_tree_.tree_.size(); ++ti) {
      for (int i = 0; i < combined_tree_.numStatesAt(ti); ++i) {
        combined_tree_.tree_[ti][i].state_value +=
            floating_leg_.trinomialTree().tree_[ti][i].state_value;
      }
    }
  }
};

}  // namespace smileexplorer

#endif  //  SMILEEXPLORER_INSTRUMENTS_SWAPS_INTEREST_RATE_SWAP_H_