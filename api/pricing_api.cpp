#include "api/pricing_api.h"

#include <cmath>
#include <limits>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "derivatives/derivative.h"
#include "derivatives/vanilla_option.h"
#include "rates/zero_curve.h"
#include "trees/binomial_tree.h"
#include "trees/propagators.h"
#include "trees/stochastic_tree_model.h"
#include "volatility/volatility.h"

namespace smileexplorer::api {
namespace {

// Each tree is stored as a dense matrix, so memory grows with the square of
// the number of timesteps. This keeps requests to a few tens of MB.
constexpr int kMaxTimesteps = 1000;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

absl::Status validate(const VanillaPricingRequest& request) {
  const BinomialTreeSpec& tree = request.tree;
  const VanillaOptionSpec& option = request.option;
  if (!(tree.spot > 0)) {
    return absl::InvalidArgumentError("spot must be positive");
  }
  if (!(tree.vol > 0)) {
    return absl::InvalidArgumentError("vol must be positive");
  }
  if (!std::isfinite(tree.rate) || !std::isfinite(tree.drift)) {
    return absl::InvalidArgumentError("rate and drift must be finite");
  }
  if (!(tree.dt_years > 0)) {
    return absl::InvalidArgumentError("dt_years must be positive");
  }
  if (!(option.strike > 0)) {
    return absl::InvalidArgumentError("strike must be positive");
  }
  if (!(option.expiry_years > 0)) {
    return absl::InvalidArgumentError("expiry_years must be positive");
  }
  if (option.expiry_years / tree.dt_years > kMaxTimesteps) {
    return absl::InvalidArgumentError(
        absl::StrCat("expiry_years / dt_years must be at most ",
                     kMaxTimesteps,
                     " timesteps"));
  }
  return absl::OkStatus();
}

template <typename PropagatorT>
absl::StatusOr<VanillaPricingResult> priceOnTree(
    const VanillaPricingRequest& request, PropagatorT propagator) {
  const BinomialTreeSpec& spec = request.tree;
  const VanillaOptionSpec& option_spec = request.option;

  // The tree must extend beyond the expiry: the final row of a tree is never
  // populated by forward propagation, and the expiry is snapped to the
  // nearest timegrid point, which may lie just after the requested expiry.
  StochasticTreeModel<PropagatorT> asset(
      BinomialTree(option_spec.expiry_years + 2 * spec.dt_years,
                   spec.dt_years),
      propagator);
  asset.forwardPropagate(Volatility(FlatVol(spec.vol)));
  const BinomialTree& asset_tree = asset.binomialTree();
  const Timegrid& timegrid = asset_tree.getTimegrid();

  const auto expiry_index =
      timegrid.getTimeIndexForExpiry(option_spec.expiry_years);
  if (!expiry_index.has_value() ||
      *expiry_index >= asset_tree.numTimesteps()) {
    return absl::InternalError("expiry does not fall within the tree");
  }
  const int ti_final = *expiry_index;

  // Flat curve; the maturities only anchor the (constant) zero rate.
  const ZeroSpotCurve curve({1.0, 10.0}, {spec.rate, spec.rate});

  VanillaPricingResult result;
  result.expiry_index = ti_final;
  result.times.resize(ti_final + 1);
  result.forward_dfs.resize(ti_final + 1, kNaN);
  const int num_nodes = numNodes(ti_final + 1);
  result.asset_values.resize(num_nodes);
  result.option_values.resize(num_nodes);
  result.exercise_values.resize(num_nodes);
  result.continuation_values.resize(num_nodes, kNaN);
  result.up_probs.resize(num_nodes, kNaN);
  result.arrow_debreu.resize(num_nodes);

  for (int t = 0; t <= ti_final; ++t) {
    result.times[t] = timegrid.time(t);
    if (t < ti_final) {
      result.forward_dfs[t] =
          curve.forwardDF(timegrid.time(t), timegrid.time(t + 1));
    }
    for (int i = 0; i <= t; ++i) {
      const int n = nodeIndex(t, i);
      result.asset_values[n] = asset_tree.nodeValue(t, i);
      if (t < ti_final) {
        const double p = asset_tree.getUpProbAt(curve, t, i);
        if (!(p > 0 && p < 1)) {
          return absl::InvalidArgumentError(absl::StrCat(
              "risk-neutral up-probability ",
              p,
              " at time index ",
              t,
              " is outside (0, 1), so the tree admits arbitrage; use a "
              "smaller timestep, a higher vol or a lower rate"));
        }
        result.up_probs[n] = p;
      }
    }
  }

  const OptionPayoff payoff =
      option_spec.is_call ? OptionPayoff::Call : OptionPayoff::Put;
  const VanillaOption option(option_spec.strike,
                             payoff,
                             option_spec.is_american ? ExerciseStyle::American
                                                     : ExerciseStyle::European);
  const VanillaOption european(
      option_spec.strike, payoff, ExerciseStyle::European);

  SingleAssetDerivative deriv(&asset_tree, &curve);
  result.price = deriv.price(option, option_spec.expiry_years);
  result.bsm_price = european.blackScholes(
      spec.spot, spec.vol, result.times[ti_final], spec.rate, 0.0);

  const BinomialTree& option_tree = deriv.binomialTree();
  const BinomialTree& arrow_debreu_tree = deriv.arrowDebreuTree();
  for (int t = 0; t <= ti_final; ++t) {
    for (int i = 0; i <= t; ++i) {
      const int n = nodeIndex(t, i);
      result.option_values[n] = option_tree.nodeValue(t, i);
      result.exercise_values[n] = option.payoff(result.asset_values[n]);
      result.arrow_debreu[n] = arrow_debreu_tree.nodeValue(t, i);
      if (t < ti_final) {
        const double p = result.up_probs[n];
        result.continuation_values[n] =
            result.forward_dfs[t] *
            (p * option_tree.nodeValue(t + 1, i + 1) +
             (1 - p) * option_tree.nodeValue(t + 1, i));
      }
    }
  }
  return result;
}

}  // namespace

absl::StatusOr<VanillaPricingResult> priceVanilla(
    const VanillaPricingRequest& request) {
  if (absl::Status status = validate(request); !status.ok()) {
    return status;
  }
  switch (request.tree.model) {
    case TreeModel::kCoxRossRubinstein:
      return priceOnTree(request, CRRPropagator(request.tree.spot));
    case TreeModel::kJarrowRudd:
      return priceOnTree(
          request,
          JarrowRuddPropagator(request.tree.drift, request.tree.spot));
  }
  return absl::InvalidArgumentError("unknown tree model");
}

}  // namespace smileexplorer::api
