#include "api/pricing_api.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

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

absl::Status validateVol(const VolSpec& vol) {
  if (!(vol.vol > 0)) {
    return absl::InvalidArgumentError("vol must be positive");
  }
  switch (vol.model) {
    case VolModel::kFlat:
      return absl::OkStatus();
    case VolModel::kTermStructure: {
      const auto& t = vol.term_maturities;
      const auto& v = vol.term_vols;
      if (t.empty() || t.size() != v.size()) {
        return absl::InvalidArgumentError(
            "term_maturities and term_vols must be non-empty and of equal "
            "length");
      }
      for (size_t k = 0; k < t.size(); ++k) {
        if (!(v[k] > 0)) {
          return absl::InvalidArgumentError("term_vols must be positive");
        }
        if (!(t[k] > (k == 0 ? 0.0 : t[k - 1]))) {
          return absl::InvalidArgumentError(
              "term_maturities must be positive and strictly increasing");
        }
        if (k > 0 && t[k] * v[k] * v[k] < t[k - 1] * v[k - 1] * v[k - 1]) {
          return absl::InvalidArgumentError(
              absl::StrCat("the forward variance between ",
                           t[k - 1],
                           "y and ",
                           t[k],
                           "y is negative: implied vols can't fall that fast"));
        }
      }
      return absl::OkStatus();
    }
    case VolModel::kSigmoidSmile:
      if (!(vol.smile_range >= 0) || !(vol.smile_stretch >= 0)) {
        return absl::InvalidArgumentError(
            "smile_range and smile_stretch must be non-negative");
      }
      return absl::OkStatus();
  }
  return absl::InvalidArgumentError("unknown vol model");
}

absl::Status validate(const VanillaPricingRequest& request) {
  const BinomialTreeSpec& tree = request.tree;
  const VanillaOptionSpec& option = request.option;
  if (!(tree.spot > 0)) {
    return absl::InvalidArgumentError("spot must be positive");
  }
  if (absl::Status status = validateVol(tree.vol); !status.ok()) {
    return status;
  }
  const bool local_vol = tree.model == TreeModel::kLocalVolatility;
  if (local_vol && tree.vol.model == VolModel::kTermStructure) {
    return absl::InvalidArgumentError(
        "the local volatility tree needs a flat or smile vol");
  }
  if (!local_vol && tree.vol.model == VolModel::kSigmoidSmile) {
    return absl::InvalidArgumentError(
        "a vol smile needs the local volatility tree model");
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
  return absl::OkStatus();
}

PiecewiseTermStructureVol termStructure(const VolSpec& vol) {
  return PiecewiseTermStructureVol(vol.term_maturities, vol.term_vols);
}

SigmoidSmileVol smile(const VolSpec& vol, double spot) {
  return {.base = vol.vol,
          .range = vol.smile_range,
          .stretch = vol.smile_stretch,
          .center = spot};
}

absl::Status validateCurve(const RateCurveSpec& curve, std::string_view name) {
  const auto& t = curve.maturities;
  const auto& r = curve.rates;
  if (t.empty() || t.size() > 10 || t.size() != r.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat(name,
                     " curve: maturities and rates must have equal length, "
                     "between 1 and 10"));
  }
  for (size_t k = 0; k < t.size(); ++k) {
    if (!std::isfinite(r[k])) {
      return absl::InvalidArgumentError(
          absl::StrCat(name, " curve: rates must be finite"));
    }
    if (!(t[k] > (k == 0 ? 0.0 : t[k - 1]))) {
      return absl::InvalidArgumentError(absl::StrCat(
          name, " curve: maturities must be positive and strictly increasing"));
    }
  }
  return absl::OkStatus();
}

ZeroSpotCurve zeroCurve(const RateCurveSpec& curve) {
  return ZeroSpotCurve(curve.maturities, curve.rates);
}

// The tree-independent inputs to priceOnTree.
struct TreeInputs {
  double spot;
  double dt_years;
  const VanillaOptionSpec& option;
  // The (domestic) discount curve.
  const RatesCurve& curve;
  // For an exchange rate, the foreign curve; otherwise null.
  const RatesCurve* foreign_curve = nullptr;
};

// The continuously-compounded zero rate to time t.
double zeroRate(const RatesCurve& curve, double t) {
  return -std::log(curve.df(t)) / t;
}

template <typename PropagatorT, typename VolSurfaceT>
absl::StatusOr<VanillaPricingResult> priceOnTree(
    const TreeInputs& inputs,
    PropagatorT propagator,
    const Volatility<VolSurfaceT>& volatility,
    double bsm_vol) {
  const VanillaOptionSpec& option_spec = inputs.option;
  const RatesCurve& curve = inputs.curve;
  const RatesCurve* foreign_curve = inputs.foreign_curve;

  // The timegrid depends on the vol (it varies with a term structure), so
  // check its size before allocating the tree.
  if (volatility.generateTimegrid(option_spec.expiry_years, inputs.dt_years)
          .size() > kMaxTimesteps + 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "the tree would need more than ", kMaxTimesteps, " timesteps"));
  }

  StochasticTreeModel<PropagatorT> asset(
      BinomialTree(option_spec.expiry_years, inputs.dt_years), propagator);
  asset.forwardPropagate(volatility);
  const BinomialTree& asset_tree = asset.binomialTree();
  const Timegrid& timegrid = asset_tree.getTimegrid();

  const auto expiry_index =
      timegrid.getTimeIndexForExpiry(option_spec.expiry_years);
  if (!expiry_index.has_value() || *expiry_index > asset_tree.numTimesteps()) {
    return absl::InternalError("expiry does not fall within the tree");
  }
  const int ti_final = *expiry_index;

  VanillaPricingResult result;
  result.expiry_index = ti_final;
  result.times.resize(ti_final + 1);
  result.forward_dfs.resize(ti_final + 1, kNaN);
  result.forward_growth.resize(ti_final + 1, kNaN);
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
      const double t0 = timegrid.time(t);
      const double t1 = timegrid.time(t + 1);
      result.forward_dfs[t] = curve.forwardDF(t0, t1);
      result.forward_growth[t] =
          foreign_curve == nullptr
              ? curve.inverseForwardDF(t0, t1)
              : curve.inverseForwardDF(t0, t1) /
                    foreign_curve->inverseForwardDF(t0, t1);
    }
    for (int i = 0; i <= t; ++i) {
      const int n = nodeIndex(t, i);
      result.asset_values[n] = asset_tree.nodeValue(t, i);
      if (t < ti_final) {
        const double p =
            foreign_curve == nullptr
                ? asset_tree.getUpProbAt(curve, t, i)
                : asset_tree.getUpProbAt(curve, *foreign_curve, t, i);
        if (!(p > 0 && p < 1)) {
          return absl::InvalidArgumentError(absl::StrCat(
              "risk-neutral up-probability ",
              p,
              " at time index ",
              t,
              " is outside (0, 1), so the tree admits arbitrage; use a "
              "smaller timestep, a higher vol or ",
              foreign_curve == nullptr
                  ? "a lower rate"
                  : "a smaller gap between the domestic and foreign rates"));
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

  std::unique_ptr<SingleAssetDerivative> deriv =
      foreign_curve == nullptr
          ? std::make_unique<SingleAssetDerivative>(&asset_tree, &curve)
          : std::make_unique<CurrencyDerivative>(
                &asset_tree, &curve, foreign_curve);
  result.price = deriv->price(option, option_spec.expiry_years);
  result.bsm_vol = bsm_vol;
  // Black-Scholes, with the foreign rate (if any) as the dividend yield: for
  // an exchange rate, this is Garman-Kohlhagen.
  const double expiry = result.times[ti_final];
  result.bsm_price = european.blackScholes(
      inputs.spot,
      bsm_vol,
      expiry,
      zeroRate(curve, expiry),
      foreign_curve == nullptr ? 0.0 : zeroRate(*foreign_curve, expiry));

  const BinomialTree& option_tree = deriv->binomialTree();
  const BinomialTree& arrow_debreu_tree = deriv->arrowDebreuTree();
  for (int t = 0; t <= ti_final; ++t) {
    for (int i = 0; i <= t; ++i) {
      const int n = nodeIndex(t, i);
      result.option_values[n] = option_tree.nodeValue(t, i);
      result.exercise_values[n] = option.payoff(result.asset_values[n]);
      result.arrow_debreu[n] = arrow_debreu_tree.nodeValue(t, i);
      if (t < ti_final) {
        const double p = result.up_probs[n];
        result.continuation_values[n] =
            result.forward_dfs[t] * (p * option_tree.nodeValue(t + 1, i + 1) +
                                     (1 - p) * option_tree.nodeValue(t + 1, i));
      }
    }
  }
  return result;
}

// Dispatches on the (validated) vol model, for the given propagator.
template <typename PropagatorT>
absl::StatusOr<VanillaPricingResult> priceWithVol(
    const VanillaPricingRequest& request,
    const RatesCurve& curve,
    PropagatorT propagator) {
  const VolSpec& vol = request.tree.vol;
  const TreeInputs inputs{.spot = request.tree.spot,
                          .dt_years = request.tree.dt_years,
                          .option = request.option,
                          .curve = curve};
  switch (vol.model) {
    case VolModel::kFlat:
      return priceOnTree(
          inputs, propagator, Volatility(FlatVol(vol.vol)), vol.vol);
    case VolModel::kTermStructure: {
      const PiecewiseTermStructureVol term_structure = termStructure(vol);
      return priceOnTree(
          inputs,
          propagator,
          Volatility(term_structure),
          term_structure.impliedVol(request.option.expiry_years));
    }
    case VolModel::kSigmoidSmile: {
      const SigmoidSmileVol smile_vol = smile(vol, request.tree.spot);
      return priceOnTree(inputs,
                         propagator,
                         Volatility(smile_vol),
                         smile_vol(request.tree.spot));
    }
  }
  return absl::InvalidArgumentError("unknown vol model");
}

}  // namespace

absl::StatusOr<VanillaPricingResult> priceVanilla(
    const VanillaPricingRequest& request) {
  if (absl::Status status = validate(request); !status.ok()) {
    return status;
  }
  const BinomialTreeSpec& tree = request.tree;
  // Flat curve; the maturities only anchor the (constant) zero rate.
  const ZeroSpotCurve curve({1.0, 10.0}, {tree.rate, tree.rate});
  switch (tree.model) {
    case TreeModel::kCoxRossRubinstein:
      return priceWithVol(request, curve, CRRPropagator(tree.spot));
    case TreeModel::kJarrowRudd:
      return priceWithVol(
          request, curve, JarrowRuddPropagator(tree.drift, tree.spot));
    case TreeModel::kLocalVolatility:
      return priceWithVol(
          request, curve, LocalVolatilityPropagator(curve, tree.spot));
  }
  return absl::InvalidArgumentError("unknown tree model");
}

absl::StatusOr<VanillaPricingResult> priceFxVanilla(
    const FxVanillaPricingRequest& request) {
  const FxTreeSpec& tree = request.tree;
  const VanillaOptionSpec& option = request.option;
  if (!(tree.spot > 0)) {
    return absl::InvalidArgumentError("spot must be positive");
  }
  if (!(tree.vol > 0)) {
    return absl::InvalidArgumentError("vol must be positive");
  }
  if (absl::Status status = validateCurve(tree.domestic, "domestic");
      !status.ok()) {
    return status;
  }
  if (absl::Status status = validateCurve(tree.foreign, "foreign");
      !status.ok()) {
    return status;
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

  const ZeroSpotCurve domestic = zeroCurve(tree.domestic);
  const ZeroSpotCurve foreign = zeroCurve(tree.foreign);
  return priceOnTree(TreeInputs{.spot = tree.spot,
                                .dt_years = tree.dt_years,
                                .option = option,
                                .curve = domestic,
                                .foreign_curve = &foreign},
                     CRRPropagator(tree.spot),
                     Volatility(FlatVol(tree.vol)),
                     tree.vol);
}

absl::StatusOr<std::vector<double>> evaluateCurve(
    const RateCurveSpec& curve, const std::vector<double>& times) {
  if (absl::Status status = validateCurve(curve, "rate"); !status.ok()) {
    return status;
  }
  const ZeroSpotCurve zero_curve = zeroCurve(curve);
  std::vector<double> dfs;
  dfs.reserve(times.size());
  for (double t : times) {
    if (!(t >= 0)) {
      return absl::InvalidArgumentError("times must be non-negative");
    }
    dfs.push_back(zero_curve.df(t));
  }
  return dfs;
}

absl::StatusOr<std::vector<double>> evaluateVol(const VolSpec& vol,
                                                double spot,
                                                const std::vector<double>& xs) {
  if (absl::Status status = validateVol(vol); !status.ok()) {
    return status;
  }
  std::vector<double> result;
  result.reserve(xs.size());
  switch (vol.model) {
    case VolModel::kFlat:
      result.assign(xs.size(), vol.vol);
      break;
    case VolModel::kTermStructure: {
      const PiecewiseTermStructureVol term_structure = termStructure(vol);
      for (double t : xs) result.push_back(term_structure(t));
      break;
    }
    case VolModel::kSigmoidSmile: {
      const SigmoidSmileVol smile_vol = smile(vol, spot);
      for (double s : xs) result.push_back(smile_vol(s));
      break;
    }
  }
  return result;
}

absl::StatusOr<double> impliedVolatility(const ImpliedVolRequest& request) {
  const ImpliedVolRequest& r = request;
  if (!(r.spot > 0) || !(r.strike > 0) || !(r.expiry_years > 0) ||
      !std::isfinite(r.rate)) {
    return absl::InvalidArgumentError(
        "spot, strike and expiry_years must be positive");
  }
  const double discounted_strike =
      r.strike * std::exp(-r.rate * r.expiry_years);
  const double lower = std::max(
      0.0, r.is_call ? r.spot - discounted_strike : discounted_strike - r.spot);
  const double upper = r.is_call ? r.spot : discounted_strike;
  if (!(r.price > lower && r.price < upper)) {
    return absl::InvalidArgumentError(
        absl::StrCat("price ",
                     r.price,
                     " is outside the no-arbitrage bounds (",
                     lower,
                     ", ",
                     upper,
                     ")"));
  }

  // Black-Scholes prices increase with vol, so bisect.
  const VanillaOption option(
      r.strike, r.is_call ? OptionPayoff::Call : OptionPayoff::Put);
  double lo = 1e-6;
  double hi = 10.0;
  for (int k = 0; k < 200 && hi - lo > 1e-12; ++k) {
    const double mid = 0.5 * (lo + hi);
    if (option.blackScholes(r.spot, mid, r.expiry_years, r.rate, 0.0) <
        r.price) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return 0.5 * (lo + hi);
}

}  // namespace smileexplorer::api
