
#ifndef SMILEEXPLORER_INTEREST_RATE_DERIVATIVE_H_
#define SMILEEXPLORER_INTEREST_RATE_DERIVATIVE_H_

#include "absl/log/log.h"
#include "derivative.h"
#include "rates/fixed_cashflow_instrument.h"
#include "rates/short_rate_tree_curve.h"
#include "vanilla_option.h"

namespace smileexplorer {

class InterestRateDerivative : public Derivative {
 public:
  InterestRateDerivative(const ShortRateTreeCurve* short_rate_curve,
                         const FixedCashflowInstrument* bond)
      : short_rate_curve_(short_rate_curve),
        bond_(bond),
        deriv_tree_(TrinomialTree::createFrom(bond_->trinomialTree())) {}

  double price(const VanillaOption& vanilla_option, double expiry_years) {
    runBackwardInduction(vanilla_option, expiry_years);
    return deriv_tree_.auxiliaryValue(0, 0);
  }

  const TrinomialTree& tree() const { return deriv_tree_; }

 private:
  const ShortRateTreeCurve* short_rate_curve_;
  const FixedCashflowInstrument* bond_;
  TrinomialTree deriv_tree_;

  template <typename OptionEvaluatorT>
  void runBackwardInduction(const OptionEvaluatorT& option_evaluator,
                            double expiry_years) {
    auto ti_final_or =
        short_rate_curve_->trinomialTree().getTimegrid().getTimeIndexForExpiry(
            expiry_years);
    if (ti_final_or == std::nullopt) {
      LOG(ERROR) << "Backward induction is impossible for requested expiry "
                 << expiry_years;
      return;
    }
    int ti_final = ti_final_or.value();

    for (int ti = ti_final; ti >= 0; --ti) {
      for (int i = 0; i < deriv_tree_.numStatesAt(ti); ++i) {
        const double deriv_value_at_node =
            option_evaluator(deriv_tree_, *bond_, ti, i, ti_final);
        deriv_tree_.setAuxiliaryValue(ti, i, deriv_value_at_node);
      }
    }
  }
};
}  // namespace smileexplorer

#endif  // SMILEEXPLORER_INTEREST_RATE_DERIVATIVE_H_