#include "api/short_rate_api.h"

#include <cmath>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "instruments/swaps/fixed_cashflow_instrument.h"
#include "rates/short_rate_tree_curve.h"
#include "rates/zero_curve.h"
#include "trees/hull_white_propagator.h"
#include "trees/trinomial_tree.h"

namespace smileexplorer::api {
namespace {

constexpr int kMaxSteps = 500;
constexpr int kMaxNodes = 200'000;

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
  if (!(spec.bond_face > 0)) {
    return absl::InvalidArgumentError("bond_face must be positive");
  }
  // evaluateCurve validates the curve.
  return evaluateCurve(spec.curve, {}).status();
}

}  // namespace

absl::StatusOr<HullWhiteTreeResult> buildHullWhiteTree(
    const HullWhiteTreeSpec& spec) {
  if (absl::Status status = validate(spec); !status.ok()) {
    return status;
  }

  // Round the horizon up to a whole number of steps (allowing for rounding
  // error, so that 0.3 / 0.1 is 3 steps, not 4).
  const int num_steps =
      static_cast<int>(std::ceil(spec.horizon_years / spec.dt_years - 1e-9));
  if (num_steps > kMaxSteps) {
    return absl::InvalidArgumentError(
        absl::StrCat("the tree would need more than ", kMaxSteps, " steps"));
  }

  auto propagator = std::make_unique<HullWhitePropagator>(
      spec.mean_reversion, spec.sigma, spec.dt_years);
  long num_nodes = 0;
  for (int t = 0; t <= num_steps; ++t) {
    num_nodes += propagator->numStatesAtTimeIndex(t);
  }
  if (num_nodes > kMaxNodes) {
    return absl::InvalidArgumentError(
        absl::StrCat("the tree would need more than ",
                     kMaxNodes,
                     " nodes; use fewer steps or faster mean reversion"));
  }

  HullWhiteTreeResult result;
  result.j_max = propagator->jMax();
  result.dr = dR(spec.sigma, spec.dt_years);

  const ZeroSpotCurve market_curve(spec.curve.maturities, spec.curve.rates);
  // TrinomialTree rounds its duration up to a whole number of steps, so pass
  // slightly less than num_steps * dt to get exactly num_steps.
  const ShortRateTreeCurve tree_curve(std::move(propagator),
                                      market_curve,
                                      num_steps * spec.dt_years * (1 - 1e-12));
  const TrinomialTree& tree = tree_curve.trinomialTree();
  if (std::ssize(tree.tree_) != num_steps + 1) {
    return absl::InternalError("unexpected number of time slices");
  }

  FixedCashflowInstrument bond(&tree_curve);
  const double maturity = tree.getTimegrid().time(num_steps);
  if (absl::Status status = bond.setCashflows(
          {Cashflow{.time_years = maturity, .amount = spec.bond_face}});
      !status.ok()) {
    return status;
  }
  result.bond_price = bond.price();
  result.market_bond_price = spec.bond_face * market_curve.df(maturity);
  const TrinomialTree& bond_tree = bond.trinomialTree();

  result.slice_offsets.push_back(0);
  for (int t = 0; t <= num_steps; ++t) {
    result.slice_offsets.push_back(result.slice_offsets.back() +
                                   tree.numStatesAt(t));
  }

  for (int t = 0; t <= num_steps; ++t) {
    const double time = tree.getTimegrid().time(t);
    result.times.push_back(time);
    result.alphas.push_back(tree.alphas_[t]);
    result.market_dfs.push_back(market_curve.df(time));
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
  return result;
}

}  // namespace smileexplorer::api
