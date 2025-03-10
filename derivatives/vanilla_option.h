#ifndef SMILEEXPLORER_DERIVATIVES_VANILLA_OPTION_H_
#define SMILEEXPLORER_DERIVATIVES_VANILLA_OPTION_H_

#include "absl/log/log.h"
#include "bsm.h"
#include "trees/binomial_tree.h"

namespace smileexplorer {

enum class OptionPayoff { Call, Put };
enum class ExerciseStyle { European, American };

// TODO expand this out.
enum class Greeks { Delta };

struct VanillaOption {
  VanillaOption(double strike,
                OptionPayoff payoff,
                ExerciseStyle style = ExerciseStyle::European)
      : strike_(strike), payoff_(payoff), style_(style) {}

  double getPayoff(double state, double strike) const {
    const double dist_from_strike =
        payoff_ == OptionPayoff::Call ? state - strike_ : strike_ - state;
    return std::max(0.0, dist_from_strike);
  }

  double blackScholes(
      double spot, double vol, double t, double r, double div) const {
    if (style_ == ExerciseStyle::American) {
      LOG(ERROR) << "No closed-form solution for American options.";
      return 0.0;
    }
    if (payoff_ == OptionPayoff::Call) {
      return call(spot, strike_, vol, t, r, div);
    } else {
      return put(spot, strike_, vol, t, r, div);
    }
  }

  double blackScholes(double spot,
                      double vol,
                      double t,
                      // Convention: fx rate is quoted as FOR-DOM:
                      const RatesCurve& foreign_rates,
                      const RatesCurve& domestic_rates) const {
    double df_dom = domestic_rates.df(t);
    double df_for = foreign_rates.df(t);
    double r_dom =
        fwdRateByPeriod(1.0, df_dom, t, CompoundingPeriod::kContinuous);
    double r_for =
        fwdRateByPeriod(1.0, df_for, t, CompoundingPeriod::kContinuous);
    return blackScholes(spot, vol, t, r_dom, r_for);
  }

  double blackScholesGreek(double spot,
                           double vol,
                           double t,
                           double r,
                           double div,
                           Greeks greek) const {
    if (style_ == ExerciseStyle::American) {
      LOG(ERROR) << "No closed-form solution for American options.";
      return 0.0;
    }
    if (greek == Greeks::Delta) {
      if (payoff_ == OptionPayoff::Call) {
        return call_delta(spot, strike_, vol, t, r, div);
      } else {
        return put_delta(spot, strike_, vol, t, r, div);
      }
    }
    return 0.0;
  }

  double blackScholesGreek(double spot,
                           double vol,
                           double t,
                           // Convention: fx rate is quoted as FOR-DOM:
                           const RatesCurve& foreign_rates,
                           const RatesCurve& domestic_rates,
                           Greeks greek) const {
    double df_dom = domestic_rates.df(t);
    double df_for = foreign_rates.df(t);
    double r_dom =
        fwdRateByPeriod(1.0, df_dom, t, CompoundingPeriod::kContinuous);
    double r_for =
        fwdRateByPeriod(1.0, df_for, t, CompoundingPeriod::kContinuous);
    return blackScholesGreek(spot, vol, t, r_dom, r_for, greek);
  }

  double operator()(const BinomialTree& deriv_tree,
                    const BinomialTree& asset_tree,
                    int ti,
                    int i,
                    int ti_final,
                    double up_prob,
                    double fwd_df) const {
    if (ti == ti_final) {
      const double state = asset_tree.nodeValue(ti, i);
      return getPayoff(state, strike_);
    }

    const double up = deriv_tree.nodeValue(ti + 1, i + 1);
    const double down = deriv_tree.nodeValue(ti + 1, i);
    const double down_prob = 1 - up_prob;

    const double discounted_expected_next_state =
        fwd_df * (up * up_prob + down * down_prob);

    if (style_ == ExerciseStyle::American) {
      const double state = asset_tree.nodeValue(ti, i);
      const double payoff_if_early_exercise = getPayoff(state, strike_);
      return std::max(payoff_if_early_exercise, discounted_expected_next_state);
    }

    return discounted_expected_next_state;
  }

 private:
  double strike_;
  OptionPayoff payoff_;
  ExerciseStyle style_;
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_DERIVATIVES_VANILLA_OPTION_H_