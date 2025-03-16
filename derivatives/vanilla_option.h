#ifndef SMILEEXPLORER_DERIVATIVES_VANILLA_OPTION_H_
#define SMILEEXPLORER_DERIVATIVES_VANILLA_OPTION_H_

#include "rates/short_rate_tree_curve.h"
#include "trees/binomial_tree.h"
#include "trees/trinomial_tree.h"

namespace smileexplorer {

enum class OptionPayoff { Call, Put };
enum class ExerciseStyle { European, American };
enum class Greeks { Delta, Vega, Gamma, Theta };

struct VanillaOption {
  VanillaOption(double strike,
                OptionPayoff payoff,
                ExerciseStyle style = ExerciseStyle::European)
      : strike_(strike), payoff_(payoff), style_(style) {}

  double blackScholes(
      double spot, double vol, double t, double r, double div) const;

  double blackScholes(double spot,
                      double vol,
                      double t,
                      // Convention: fx rate is quoted as FOR-DOM:
                      const RatesCurve& foreign_rates,
                      const RatesCurve& domestic_rates) const {
    const auto [r_for, r_dom] =
        dualCurrencyRates(t, foreign_rates, domestic_rates);
    return blackScholes(spot, vol, t, r_dom, r_for);
  }

  double blackScholesGreek(double spot,
                           double vol,
                           double t,
                           double r,
                           double div,
                           Greeks greek) const;

  double blackScholesGreek(double spot,
                           double vol,
                           double t,
                           // Convention: fx rate is quoted as FOR-DOM:
                           const RatesCurve& foreign_rates,
                           const RatesCurve& domestic_rates,
                           Greeks greek) const {
    const auto [r_for, r_dom] =
        dualCurrencyRates(t, foreign_rates, domestic_rates);
    return blackScholesGreek(spot, vol, t, r_dom, r_for, greek);
  }

  double operator()(const BinomialTree& deriv_tree,
                    const BinomialTree& asset_tree,
                    int ti,
                    int i,
                    int ti_final,
                    double up_prob,
                    double fwd_df) const;

  double operator()(const TrinomialTree& deriv_tree,
                    const ShortRateTreeCurve& short_rate_curve,
                    int ti,
                    int i,
                    int ti_final) const;

 private:
  double strike_;
  OptionPayoff payoff_;
  ExerciseStyle style_;

  double getPayoff(double state, double strike) const {
    const double dist_from_strike =
        payoff_ == OptionPayoff::Call ? state - strike_ : strike_ - state;
    return std::max(0.0, dist_from_strike);
  }
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_DERIVATIVES_VANILLA_OPTION_H_