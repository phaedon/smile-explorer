
#ifndef SMILEEXPLORER_DERIVATIVES_DERIVATIVE_H_
#define SMILEEXPLORER_DERIVATIVES_DERIVATIVE_H_

#include "derivative.h"
#include "vanilla_option.h"

namespace smileexplorer {
class InterestRateDerivative : public Derivative {
 public:
  double price(const VanillaOption& vanilla_option,
               double expiry_years) override {
    static_assert("Unimplemented!");
    return -1;
  }

 private:
  template <typename OptionEvaluatorT>
  void runBackwardInduction(const OptionEvaluatorT& option_evaluator,
                            double expiry_years);
};
}  // namespace smileexplorer

#endif  // SMILEEXPLORER_DERIVATIVES_INTEREST_RATE_DERIVATIVE_H_