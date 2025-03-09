#ifndef MARKETS_DERIVATIVE_H_
#define MARKETS_DERIVATIVE_H_

#include "absl/log/log.h"
#include "rates/rates_curve.h"
#include "trees/binomial_tree.h"

namespace markets {

enum class OptionPayoff { Call, Put };
enum class ExerciseStyle { European, American };

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

  double strike_;
  OptionPayoff payoff_;
  ExerciseStyle style_;
};

class Derivative {
 public:
  Derivative(const BinomialTree* asset_tree, const RatesCurve* curve)
      : deriv_tree_(BinomialTree::createFrom(*asset_tree)),
        arrow_debreu_tree_(BinomialTree::createFrom(*asset_tree)),
        asset_tree_(asset_tree),
        curve_(curve) {}

  // This is the Arrow-Debreu pricing method but it is only relevant for
  // Europeans. Filing it away for now.
  //
  // double price(const std::function<double(double)>& payoff_fn,
  //              double expiry_years) {
  //   updateArrowDebreuPrices();
  //   double price = 0.;
  //   auto t_final_or =
  //       deriv_tree_.getTimegrid().getTimeIndexForExpiry(expiry_years);
  //   int t_final = t_final_or.value();
  //   // Set the payoff at each scenario on the maturity date.
  //   for (int i = 0; i <= t_final; ++i) {
  //     price += payoff_fn(asset_tree_->nodeValue(t_final, i)) *
  //              arrow_debreu_tree_.nodeValue(t_final, i);
  //   }
  //   return price;
  // }

  double price(const VanillaOption& vanilla_option, double expiry_years) {
    runBackwardInduction(vanilla_option, expiry_years);
    return deriv_tree_.nodeValue(0, 0);
  }

  const BinomialTree& binomialTree() const { return deriv_tree_; }

  // Exposed for testing.
  const BinomialTree& arrowDebreuTree() {
    updateArrowDebreuPrices();
    return arrow_debreu_tree_;
  }

 private:
  BinomialTree deriv_tree_;
  BinomialTree arrow_debreu_tree_;

 protected:
  // Not owned. These are underlying securities and general market conditions.
  const BinomialTree* asset_tree_;
  const RatesCurve* curve_;

 private:
  virtual double getUpProbAt(int time_index, int i) const {
    return asset_tree_->getUpProbAt(*curve_, time_index, i);
  }

  double forwardDF(int t) const {
    const auto& timegrid = asset_tree_->getTimegrid();
    return curve_->forwardDF(timegrid.time(t), timegrid.time(t + 1));
  }

  void updateArrowDebreuPrices() {
    arrow_debreu_tree_.setValue(0, 0, 1.0);

    for (int ti = 1; ti < arrow_debreu_tree_.numTimesteps(); ++ti) {
      for (int i = 0; i <= ti; ++i) {
        double prev_down =
            i == 0 ? 0 : arrow_debreu_tree_.nodeValue(ti - 1, i - 1);
        double prev_up = i == ti ? 0 : arrow_debreu_tree_.nodeValue(ti - 1, i);

        // It's necessary to retrieve transition probabilities from different
        // nodes in case of local vol.
        double q_prev_down = i == 0 ? 0 : getUpProbAt(ti - 1, i - 1);
        double q_prev_up = i == ti ? 0 : getUpProbAt(ti - 1, i);

        double ad_price = forwardDF(ti - 1) *
                          (q_prev_down * prev_down + (1 - q_prev_up) * prev_up);
        arrow_debreu_tree_.setValue(ti, i, ad_price);
      }
    }
  }

  template <typename OptionEvaluatorT>
  void runBackwardInduction(const OptionEvaluatorT& option_evaluator,
                            double expiry_years) {
    auto ti_final_or =
        deriv_tree_.getTimegrid().getTimeIndexForExpiry(expiry_years);
    if (ti_final_or == std::nullopt) {
      LOG(ERROR) << "Backward induction is impossible for requested expiry "
                 << expiry_years;
      return;
    }
    int ti_final = ti_final_or.value();
    deriv_tree_.setZeroAfterIndex(ti_final);

    for (int ti = ti_final; ti >= 0; --ti) {
      for (int i = 0; i <= ti; ++i) {
        const double up_prob = getUpProbAt(ti, i);
        const double fwd_df = forwardDF(ti);
        const double deriv_value_at_node = option_evaluator(
            deriv_tree_, *asset_tree_, ti, i, ti_final, up_prob, fwd_df);
        deriv_tree_.setValue(ti, i, deriv_value_at_node);
      }
    }
  }
};

class CurrencyDerivative : public Derivative {
 public:
  CurrencyDerivative(const BinomialTree* asset_tree,
                     const RatesCurve* domestic_curve,
                     const RatesCurve* foreign_curve)
      : Derivative(asset_tree, domestic_curve), foreign_curve_(foreign_curve) {}

  double getUpProbAt(int time_index, int i) const override {
    return this->asset_tree_->getUpProbAt(
        *this->curve_, *foreign_curve_, time_index, i);
  }

 private:
  const RatesCurve* foreign_curve_;
};

}  // namespace markets

#endif  // MARKETS_DERIVATIVE_H_