
#ifndef SMILEEXPLORER_INTEREST_RATE_DERIVATIVE_H_
#define SMILEEXPLORER_INTEREST_RATE_DERIVATIVE_H_

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "derivative.h"
#include "instruments/swaps/interest_rate_swap.h"
#include "rates/short_rate_tree_curve.h"
#include "vanilla_option.h"

namespace smileexplorer {

// A date on which a Bermudan option can be exercised. Exercising pays the
// option's payoff on the underlying's value there, less `cashflow_excluded`: a
// cashflow paid on that date which belongs to the period before exercise. (For
// a swaption exercised on a coupon date, the coupon ending the previous
// period: the swap entered starts after it.)
struct ExerciseDate {
  double time_years = 0.0;
  double cashflow_excluded = 0.0;
};

class InterestRateDerivative : public Derivative {
 public:
  InterestRateDerivative(const ShortRateTreeCurve* short_rate_curve,
                         const InterestRateSwap* swap)
      : short_rate_curve_(short_rate_curve),
        swap_(swap),
        deriv_tree_(TrinomialTree::createFrom(swap_->trinomialTree())) {}

  double price(const VanillaOption& vanilla_option, double expiry_years) {
    runBackwardInduction(vanilla_option, expiry_years);
    return deriv_tree_.nodeValue(0, 0);
  }

  // Prices an option exercisable only on the given dates (in increasing
  // order, on the tree's steps): a Bermudan. The option's exercise style is
  // ignored; its payoff and strike are used on each date.
  absl::StatusOr<double> priceBermudan(const VanillaOption& option,
                                       const std::vector<ExerciseDate>& dates) {
    if (dates.empty()) {
      return absl::InvalidArgumentError("a Bermudan needs an exercise date");
    }
    const TrinomialTree& rates = short_rate_curve_->trinomialTree();
    const Timegrid& grid = rates.getTimegrid();
    // The exercise value to subtract at each time index, or NaN where the
    // option can't be exercised.
    std::vector<double> excluded(deriv_tree_.tree_.size(),
                                 std::numeric_limits<double>::quiet_NaN());
    int ti_final = -1;
    for (const ExerciseDate& date : dates) {
      const auto index = grid.getTimeIndexForExpiry(date.time_years);
      if (!index.has_value() ||
          std::abs(grid.time(*index) - date.time_years) > 1e-9) {
        return absl::InvalidArgumentError(
            absl::StrCat("the exercise date ",
                         date.time_years,
                         " doesn't fall on one of the tree's steps"));
      }
      if (*index <= ti_final) {
        return absl::InvalidArgumentError(
            "exercise dates must be strictly increasing");
      }
      ti_final = *index;
      excluded[ti_final] = date.cashflow_excluded;
    }

    const TrinomialTree& underlying = swap_->trinomialTree();
    for (int ti = ti_final; ti >= 0; --ti) {
      for (int i = 0; i < deriv_tree_.numStatesAt(ti); ++i) {
        double value = 0.0;
        if (ti < ti_final) {
          const auto& node = deriv_tree_.tree_[ti][i];
          const auto& next = deriv_tree_.getSuccessorNodes(node, ti, i);
          value = std::exp(-rates.shortRate(ti, i) * grid.dt(ti)) *
                  (node.branch_probs.pu * next.up.state_value +
                   node.branch_probs.pm * next.mid.state_value +
                   node.branch_probs.pd * next.down.state_value);
        }
        if (!std::isnan(excluded[ti])) {
          value = std::max(
              value, option.payoff(underlying.nodeValue(ti, i) - excluded[ti]));
        }
        deriv_tree_.setNodeValue(ti, i, value);
      }
    }
    return deriv_tree_.nodeValue(0, 0);
  }

  const TrinomialTree& tree() const { return deriv_tree_; }

 private:
  const ShortRateTreeCurve* short_rate_curve_;
  const InterestRateSwap* swap_;
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
            option_evaluator(deriv_tree_, *swap_, ti, i, ti_final);
        deriv_tree_.setNodeValue(ti, i, deriv_value_at_node);
      }
    }
  }
};
}  // namespace smileexplorer

#endif  // SMILEEXPLORER_INTEREST_RATE_DERIVATIVE_H_