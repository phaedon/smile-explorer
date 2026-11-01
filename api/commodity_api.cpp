#include "api/commodity_api.h"

#include <cmath>
#include <limits>
#include <optional>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "commodities/commodity_tree.h"
#include "commodities/futures_curve.h"
#include "derivatives/bsm.h"
#include "trees/hull_white_propagator.h"

namespace smileexplorer::api {
namespace {

constexpr int kMaxPillars = 60;
constexpr int kMaxStepsPerMonth = 16;
constexpr double kMaxHorizonYears = 3.0;
constexpr int kMaxNodes = 200'000;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
// The 95% quantile of the standard normal.
constexpr double kZ95 = 1.6448536269514722;

double dtOf(const CommoditySpec& spec) {
  return 1.0 / (12.0 * spec.steps_per_month);
}

int numSteps(const CommoditySpec& spec) {
  return static_cast<int>(std::ceil(spec.horizon_years / dtOf(spec) - 1e-9));
}

absl::Status validate(const CommoditySpec& spec) {
  if (!(spec.spot > 0) || !std::isfinite(spec.spot)) {
    return absl::InvalidArgumentError("spot must be positive");
  }
  const auto& times = spec.futures_times;
  const auto& prices = spec.futures_prices;
  if (times.empty() || std::ssize(times) > kMaxPillars ||
      times.size() != prices.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("the futures curve needs between 1 and ",
                     kMaxPillars,
                     " pillars, with one price each"));
  }
  for (size_t k = 0; k < times.size(); ++k) {
    if (!std::isfinite(times[k]) || !(times[k] > (k == 0 ? 0 : times[k - 1]))) {
      return absl::InvalidArgumentError(
          "futures_times must be positive and increasing");
    }
    if (!(prices[k] > 0) || !std::isfinite(prices[k])) {
      return absl::InvalidArgumentError("futures prices must be positive");
    }
  }
  if (!(spec.rate >= -0.1 && spec.rate <= 0.5)) {
    return absl::InvalidArgumentError("rate must be between -10% and 50%");
  }
  if (!(spec.mean_reversion > 0 && spec.mean_reversion <= 20)) {
    return absl::InvalidArgumentError(
        "mean_reversion must be positive and at most 20");
  }
  if (!(spec.sigma > 0 && spec.sigma <= 3)) {
    return absl::InvalidArgumentError("sigma must be positive and at most 3");
  }
  if (spec.steps_per_month < 1 || spec.steps_per_month > kMaxStepsPerMonth) {
    return absl::InvalidArgumentError(absl::StrCat(
        "steps_per_month must be between 1 and ", kMaxStepsPerMonth));
  }
  if (!(spec.horizon_years > 0 && spec.horizon_years <= kMaxHorizonYears)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "horizon_years must be positive and at most ", kMaxHorizonYears));
  }
  // The tree stops widening at j_max = ceil(0.184 / (a dt)), so slow mean
  // reversion means wide slices.
  const HullWhitePropagator propagator(
      spec.mean_reversion, spec.sigma, dtOf(spec));
  long num_nodes = 0;
  for (int t = 0; t <= numSteps(spec); ++t) {
    num_nodes += propagator.numStatesAtTimeIndex(t);
  }
  if (num_nodes > kMaxNodes) {
    return absl::InvalidArgumentError(
        absl::StrCat("the tree would need more than ",
                     kMaxNodes,
                     " nodes; use fewer steps or faster mean reversion"));
  }
  return absl::OkStatus();
}

FuturesCurve curveOf(const CommoditySpec& spec) {
  return FuturesCurve(spec.spot, spec.futures_times, spec.futures_prices);
}

CommodityTree treeOf(const CommoditySpec& spec, const FuturesCurve& curve) {
  return CommodityTree(
      curve, spec.mean_reversion, spec.sigma, dtOf(spec), spec.horizon_years);
}

// The tree's time index for `years`, if it falls on a step.
std::optional<int> stepAt(const CommodityTree& tree, double years) {
  const double steps = years / tree.dt();
  const int index = static_cast<int>(std::round(steps));
  if (!std::isfinite(steps) || index < 0 || index >= tree.numSlices() ||
      std::abs(index - steps) > 1e-6) {
    return std::nullopt;
  }
  return index;
}

}  // namespace

absl::StatusOr<CommodityTreeResult> buildCommodityTree(
    const CommoditySpec& spec) {
  if (absl::Status status = validate(spec); !status.ok()) {
    return status;
  }
  const FuturesCurve curve = curveOf(spec);
  const CommodityTree tree = treeOf(spec, curve);

  CommodityTreeResult result;
  result.dx = tree.dx();
  result.j_max = tree.jMax();

  const double a = spec.mean_reversion;
  const double sigma = spec.sigma;
  result.slice_offsets.push_back(0);
  for (int t = 0; t < tree.numSlices(); ++t) {
    const double time = tree.time(t);
    result.times.push_back(time);
    result.alphas.push_back(tree.alpha(t));
    const double f = curve.price(time);
    result.curve_prices.push_back(f);
    result.expected_spots.push_back(tree.expectedSpot(t));

    // ln S(t) is normal with variance sigma^2 (1 - exp(-2at)) / (2a), and its
    // mean puts E[S(t)] on the futures curve.
    const double var = sigma * sigma * -std::expm1(-2 * a * time) / (2 * a);
    const double gbm_var = sigma * sigma * time;
    result.spot_q05.push_back(f * std::exp(-var / 2 - kZ95 * std::sqrt(var)));
    result.spot_q50.push_back(f * std::exp(-var / 2));
    result.spot_q95.push_back(f * std::exp(-var / 2 + kZ95 * std::sqrt(var)));
    result.gbm_q05.push_back(
        f * std::exp(-gbm_var / 2 - kZ95 * std::sqrt(gbm_var)));
    result.gbm_q50.push_back(f * std::exp(-gbm_var / 2));
    result.gbm_q95.push_back(
        f * std::exp(-gbm_var / 2 + kZ95 * std::sqrt(gbm_var)));

    const int num_states = tree.numStates(t);
    result.slice_offsets.push_back(result.slice_offsets.back() + num_states);
    for (int i = 0; i < num_states; ++i) {
      const TrinomialNode& node = tree.tree().node(t, i);
      result.j.push_back(i - (num_states - 1) / 2);
      result.x.push_back(tree.x(t, i));
      result.spots.push_back(tree.spot(t, i));
      result.probabilities.push_back(tree.probability(t, i));
      result.pu.push_back(node.branch_probs.pu);
      result.pm.push_back(node.branch_probs.pm);
      result.pd.push_back(node.branch_probs.pd);
      result.branch_style.push_back(static_cast<int>(node.branch_style));
      result.successor_mid.push_back(t + 1 < tree.numSlices()
                                         ? result.slice_offsets[t + 1] +
                                               tree.successorMid(t, i)
                                         : -1);
    }
  }

  for (size_t k = 0; k < spec.futures_times.size(); ++k) {
    const double T = spec.futures_times[k];
    result.implied_carry.push_back(curve.impliedCarry(T));
    result.futures_vols.push_back(instantaneousFuturesVol(a, sigma, T));
    result.option_vols.push_back(samuelsonImpliedVol(a, sigma, T, T));
  }
  return result;
}

absl::StatusOr<CommodityOptionResult> priceCommodityOption(
    const CommodityOptionRequest& request) {
  const CommoditySpec& spec = request.commodity;
  const CommodityOptionSpec& option = request.option;
  if (absl::Status status = validate(spec); !status.ok()) {
    return status;
  }
  if (!(option.strike > 0) || !std::isfinite(option.strike)) {
    return absl::InvalidArgumentError("strike must be positive");
  }
  const FuturesCurve curve = curveOf(spec);
  const CommodityTree tree = treeOf(spec, curve);
  const std::optional<int> delivery = stepAt(tree, option.delivery_years);
  if (!delivery.has_value() || *delivery < 1) {
    return absl::InvalidArgumentError(
        "delivery_years must fall on one of the tree's steps, after today and "
        "within its horizon");
  }
  const std::optional<int> expiry = stepAt(tree, option.expiry_years);
  if (!expiry.has_value() || *expiry < 1 || *expiry > *delivery) {
    return absl::InvalidArgumentError(
        "expiry_years must fall on one of the tree's steps, after today and "
        "no later than delivery");
  }

  const OptionOnFuturesValues values =
      tree.optionOnFutures({.delivery_index = *delivery,
                            .expiry_index = *expiry,
                            .strike = option.strike,
                            .is_call = option.is_call,
                            .is_american = option.is_american},
                           spec.rate);

  CommodityOptionResult result;
  result.delivery_index = *delivery;
  result.expiry_index = *expiry;
  result.futures_price = values.futures[0][0];
  result.price = values.price;
  result.european_price = values.european_price;

  const double t0 = tree.time(*expiry);
  const double T = tree.time(*delivery);
  result.implied_vol =
      samuelsonImpliedVol(spec.mean_reversion, spec.sigma, t0, T);
  const double f = curve.price(T);
  result.black_price =
      option.is_call
          ? call(f, option.strike, result.implied_vol, t0, spec.rate, spec.rate)
          : put(f, option.strike, result.implied_vol, t0, spec.rate, spec.rate);

  for (int t = 0; t < tree.numSlices(); ++t) {
    for (int i = 0; i < tree.numStates(t); ++i) {
      const bool live = t <= *expiry;
      result.futures_values.push_back(live ? values.futures[t][i] : kNaN);
      result.option_values.push_back(live ? values.values[t][i] : kNaN);
      result.exercised.push_back(live && values.exercised[t][i] ? 1 : 0);
    }
  }
  return result;
}

}  // namespace smileexplorer::api
