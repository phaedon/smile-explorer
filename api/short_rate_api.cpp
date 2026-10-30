#include "api/short_rate_api.h"

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "derivatives/hull_white_bond_option.h"
#include "derivatives/interest_rate_derivative.h"
#include "derivatives/vanilla_option.h"
#include "instruments/swaps/fixed_cashflow_instrument.h"
#include "instruments/swaps/interest_rate_swap.h"
#include "rates/short_rate_tree_curve.h"
#include "rates/zero_curve.h"
#include "trees/hull_white_propagator.h"
#include "trees/trinomial_tree.h"

namespace smileexplorer::api {
namespace {

constexpr int kMaxSteps = 500;
constexpr int kMaxNodes = 200'000;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

absl::Status validate(const HullWhiteTreeSpec& spec) {
  if (!(spec.mean_reversion > 0)) {
    return absl::InvalidArgumentError("mean_reversion must be positive");
  }
  if (!(spec.sigma > 0)) {
    return absl::InvalidArgumentError("sigma must be positive");
  }
  if (!(spec.dt_years > 0) || !(spec.horizon_years > 0)) {
    return absl::InvalidArgumentError(
        "dt_years and horizon_years must be positive");
  }
  // Check the sizes in floating point, before anything is converted to int.
  const double steps = spec.horizon_years / spec.dt_years;
  if (!std::isfinite(steps) || steps > kMaxSteps + 1e-9) {
    return absl::InvalidArgumentError(
        absl::StrCat("the tree would need more than ", kMaxSteps, " steps"));
  }
  // The tree stops widening at j_max = ceil(0.184 / (a dt)) (see
  // HullWhitePropagator::jMax), so that bounds the states per step.
  const double j_max = 0.184 / (spec.mean_reversion * spec.dt_years);
  if (!std::isfinite(j_max) || j_max > kMaxNodes) {
    return absl::InvalidArgumentError(
        "mean reversion is too slow for this timestep: use faster mean "
        "reversion or longer steps");
  }
  if (!(spec.bond_face > 0)) {
    return absl::InvalidArgumentError("bond_face must be positive");
  }
  // evaluateCurve validates the curve.
  return evaluateCurve(spec.curve, {}).status();
}

// Validates the spec and returns the number of steps in the tree.
absl::StatusOr<int> numSteps(const HullWhiteTreeSpec& spec) {
  if (absl::Status status = validate(spec); !status.ok()) {
    return status;
  }

  // Round the horizon up to a whole number of steps (allowing for rounding
  // error, so that 0.3 / 0.1 is 3 steps, not 4).
  const int num_steps =
      static_cast<int>(std::ceil(spec.horizon_years / spec.dt_years - 1e-9));

  const HullWhitePropagator propagator(
      spec.mean_reversion, spec.sigma, spec.dt_years);
  long num_nodes = 0;
  for (int t = 0; t <= num_steps; ++t) {
    num_nodes += propagator.numStatesAtTimeIndex(t);
  }
  if (num_nodes > kMaxNodes) {
    return absl::InvalidArgumentError(
        absl::StrCat("the tree would need more than ",
                     kMaxNodes,
                     " nodes; use fewer steps or faster mean reversion"));
  }
  return num_steps;
}

// A Hull-White tree fitted to the market curve, with a zero-coupon bond
// maturing at its end priced on it. Held by pointer: the bond refers to the
// tree.
struct FittedTree {
  std::unique_ptr<ZeroSpotCurve> market_curve;
  std::unique_ptr<ShortRateTreeCurve> tree_curve;
  std::unique_ptr<FixedCashflowInstrument> bond;
  HullWhiteTreeResult result;
};

absl::StatusOr<FittedTree> fitTree(const HullWhiteTreeSpec& spec) {
  const absl::StatusOr<int> num_steps_or = numSteps(spec);
  if (!num_steps_or.ok()) {
    return num_steps_or.status();
  }
  const int num_steps = *num_steps_or;

  FittedTree fitted;
  HullWhiteTreeResult& result = fitted.result;
  auto propagator = std::make_unique<HullWhitePropagator>(
      spec.mean_reversion, spec.sigma, spec.dt_years);
  result.j_max = propagator->jMax();
  result.dr = dR(spec.sigma, spec.dt_years);

  fitted.market_curve =
      std::make_unique<ZeroSpotCurve>(spec.curve.maturities, spec.curve.rates);
  // TrinomialTree rounds its duration up to a whole number of steps, so pass
  // slightly less than num_steps * dt to get exactly num_steps.
  fitted.tree_curve = std::make_unique<ShortRateTreeCurve>(
      std::move(propagator),
      *fitted.market_curve,
      num_steps * spec.dt_years * (1 - 1e-12));
  const TrinomialTree& tree = fitted.tree_curve->trinomialTree();
  if (std::ssize(tree.tree_) != num_steps + 1) {
    return absl::InternalError("unexpected number of time slices");
  }

  fitted.bond =
      std::make_unique<FixedCashflowInstrument>(fitted.tree_curve.get());
  const double maturity = tree.getTimegrid().time(num_steps);
  if (absl::Status status = fitted.bond->setCashflows(
          {Cashflow{.time_years = maturity, .amount = spec.bond_face}});
      !status.ok()) {
    return status;
  }
  result.bond_price = fitted.bond->price();
  result.market_bond_price = spec.bond_face * fitted.market_curve->df(maturity);
  const TrinomialTree& bond_tree = fitted.bond->trinomialTree();

  result.slice_offsets.push_back(0);
  for (int t = 0; t <= num_steps; ++t) {
    result.slice_offsets.push_back(result.slice_offsets.back() +
                                   tree.numStatesAt(t));
  }

  for (int t = 0; t <= num_steps; ++t) {
    const double time = tree.getTimegrid().time(t);
    result.times.push_back(time);
    result.alphas.push_back(tree.alphas_[t]);
    result.market_dfs.push_back(fitted.market_curve->df(time));
    result.tree_dfs.push_back(tree.arrowDebreuSumAtTimestep(t));

    const int num_states = tree.numStatesAt(t);
    for (int i = 0; i < num_states; ++i) {
      const TrinomialNode& node = tree.node(t, i);
      result.j.push_back(i - (num_states - 1) / 2);
      result.x.push_back(node.state_value);
      result.short_rates.push_back(tree.shortRate(t, i));
      result.pu.push_back(node.branch_probs.pu);
      result.pm.push_back(node.branch_probs.pm);
      result.pd.push_back(node.branch_probs.pd);
      result.branch_style.push_back(static_cast<int>(node.branch_style));
      result.arrow_debreu.push_back(node.arrow_debreu);
      result.bond_values.push_back(bond_tree.nodeValue(t, i));
      if (t < num_steps) {
        // Locate the middle successor in the next slice, using the tree's
        // own branching rules.
        const auto successors = tree.getSuccessorNodes(node, t, i);
        result.successor_mid.push_back(
            result.slice_offsets[t + 1] +
            static_cast<int>(&successors.mid - &tree.tree_[t + 1][0]));
      } else {
        result.successor_mid.push_back(-1);
      }
    }
  }
  return fitted;
}

}  // namespace

absl::StatusOr<HullWhiteTreeResult> buildHullWhiteTree(
    const HullWhiteTreeSpec& spec) {
  absl::StatusOr<FittedTree> fitted = fitTree(spec);
  if (!fitted.ok()) {
    return fitted.status();
  }
  return std::move(fitted->result);
}

absl::StatusOr<BondOptionResult> priceBondOption(
    const BondOptionRequest& request) {
  const BondOptionSpec& option_spec = request.option;
  if (!(option_spec.strike > 0)) {
    return absl::InvalidArgumentError("strike must be positive");
  }
  if (!(option_spec.expiry_years > 0)) {
    return absl::InvalidArgumentError("expiry_years must be positive");
  }
  absl::StatusOr<FittedTree> fitted = fitTree(request.tree);
  if (!fitted.ok()) {
    return fitted.status();
  }
  const TrinomialTree& tree = fitted->tree_curve->trinomialTree();
  const int num_steps = std::ssize(tree.tree_) - 1;
  const std::optional<int> expiry_index =
      tree.getTimegrid().getTimeIndexForExpiry(option_spec.expiry_years);
  if (!expiry_index.has_value() || *expiry_index < 1 ||
      *expiry_index >= num_steps) {
    return absl::InvalidArgumentError(
        "the option must expire at least one step after today and before "
        "the bond matures");
  }
  const int ti_final = *expiry_index;
  const double expiry = tree.getTimegrid().time(ti_final);

  BondOptionResult result;
  result.expiry_index = ti_final;
  const HullWhiteTreeSpec& spec = request.tree;
  const double maturity = tree.getTimegrid().time(num_steps);
  result.analytic_price = hullWhiteZeroBondOption(*fitted->market_curve,
                                                  spec.mean_reversion,
                                                  spec.sigma,
                                                  expiry,
                                                  maturity,
                                                  spec.bond_face,
                                                  option_spec.strike,
                                                  option_spec.is_call);

  const InterestRateSwap bond = InterestRateSwap::createBond(*fitted->bond);
  const OptionPayoff payoff =
      option_spec.is_call ? OptionPayoff::Call : OptionPayoff::Put;
  const VanillaOption option(option_spec.strike,
                             payoff,
                             option_spec.is_american ? ExerciseStyle::American
                                                     : ExerciseStyle::European);
  InterestRateDerivative deriv(fitted->tree_curve.get(), &bond);
  result.price = deriv.price(option, expiry);
  InterestRateDerivative european(fitted->tree_curve.get(), &bond);
  result.european_price =
      european.price(VanillaOption(option_spec.strike, payoff), expiry);

  // Per-node values, up to expiry.
  const HullWhiteTreeResult& nodes = fitted->result;
  const int num_nodes = nodes.slice_offsets.back();
  result.option_values.assign(num_nodes, kNaN);
  result.exercise_values.assign(num_nodes, kNaN);
  result.continuation_values.assign(num_nodes, kNaN);
  for (int t = 0; t <= ti_final; ++t) {
    for (int i = 0; i < tree.numStatesAt(t); ++i) {
      const int n = nodes.slice_offsets[t] + i;
      result.option_values[n] = deriv.tree().nodeValue(t, i);
      result.exercise_values[n] = option.payoff(nodes.bond_values[n]);
      if (t < ti_final) {
        // The option's value at node index k of the next slice.
        const auto next = [&](int k) {
          return deriv.tree().nodeValue(t + 1, k - nodes.slice_offsets[t + 1]);
        };
        const int m = nodes.successor_mid[n];
        result.continuation_values[n] =
            std::exp(-nodes.short_rates[n] * spec.dt_years) *
            (nodes.pu[n] * next(m + 1) + nodes.pm[n] * next(m) +
             nodes.pd[n] * next(m - 1));
      }
    }
  }
  result.tree = std::move(fitted->result);
  return result;
}

}  // namespace smileexplorer::api
