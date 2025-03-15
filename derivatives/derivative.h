#ifndef SMILEEXPLORER_DERIVATIVES_DERIVATIVE_H_
#define SMILEEXPLORER_DERIVATIVES_DERIVATIVE_H_

#include "absl/log/log.h"
#include "rates/rates_curve.h"
#include "trees/binomial_tree.h"
#include "vanilla_option.h"

namespace smileexplorer {

class Derivative {
  virtual double price(const VanillaOption& vanilla_option,
                       double expiry_years) = 0;
};

class SingleAssetDerivative : public Derivative {
 public:
  SingleAssetDerivative(const BinomialTree* asset_tree, const RatesCurve* curve)
      : deriv_tree_(BinomialTree::createFrom(*asset_tree)),
        arrow_debreu_tree_(BinomialTree::createFrom(*asset_tree)),
        asset_tree_(asset_tree),
        curve_(curve) {}

  double price(const VanillaOption& vanilla_option,
               double expiry_years) override {
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

class CurrencyDerivative : public SingleAssetDerivative {
 public:
  CurrencyDerivative(const BinomialTree* asset_tree,
                     const RatesCurve* domestic_curve,
                     const RatesCurve* foreign_curve)
      : SingleAssetDerivative(asset_tree, domestic_curve),
        foreign_curve_(foreign_curve) {}

 private:
  const RatesCurve* foreign_curve_;

  double getUpProbAt(int time_index, int i) const override {
    return this->asset_tree_->getUpProbAt(
        *this->curve_, *foreign_curve_, time_index, i);
  }
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_DERIVATIVES_DERIVATIVE_H_