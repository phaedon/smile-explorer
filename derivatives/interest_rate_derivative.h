
#ifndef SMILEEXPLORER_INTEREST_RATE_DERIVATIVE_H_
#define SMILEEXPLORER_INTEREST_RATE_DERIVATIVE_H_

#include "absl/log/log.h"
#include "derivative.h"
#include "rates/tree_curves.h"
#include "vanilla_option.h"

namespace smileexplorer {

class InterestRateDerivative : public Derivative {
 public:
  InterestRateDerivative(ShortRateTreeCurve* short_rate_curve)
      : short_rate_curve_(short_rate_curve) {}

  double price(const VanillaOption& vanilla_option,
               double expiry_years) override {
    runBackwardInduction(vanilla_option, expiry_years);

    static_assert("Unimplemented!");
    return -1;
  }

 private:
  // Warning! This is no longer thread-safe and very dangerous! Just a temporary
  // hack until we implement createFrom() for trinomial trees.
  ShortRateTreeCurve* short_rate_curve_;

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
    short_rate_curve_->trinomialTree().setZeroAfterIndex(ti_final);

    static_assert("Unimplemented!");
  }
};
}  // namespace smileexplorer

#endif  // SMILEEXPLORER_INTEREST_RATE_DERIVATIVE_H_