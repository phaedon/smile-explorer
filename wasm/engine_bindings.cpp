// JavaScript bindings (via Embind) for the pricing API in //api, compiled to
// WebAssembly by //wasm:engine.
//
// Requests are plain JS objects that map field-for-field onto the C++ request
// structs. Results come back as plain objects whose per-node data are
// Float64Arrays, indexed as described in api/pricing_api.h. TypeScript
// definitions for all of this are generated at link time (engine_lib.d.ts).

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "api/greeks_api.h"
#include "api/pricing_api.h"
#include "api/rates_options_api.h"
#include "api/short_rate_api.h"

namespace smileexplorer::api {
namespace {

using emscripten::val;

EMSCRIPTEN_DECLARE_VAL_TYPE(Float64Array);
EMSCRIPTEN_DECLARE_VAL_TYPE(Int32Array);
EMSCRIPTEN_DECLARE_VAL_TYPE(NumberArray);
EMSCRIPTEN_DECLARE_VAL_TYPE(QuoteArray);

Float64Array toFloat64Array(const std::vector<double>& values) {
  // Constructing a Float64Array from a view copies it out of wasm memory.
  return Float64Array(
      val::global("Float64Array")
          .new_(emscripten::typed_memory_view(values.size(), values.data())));
}

Int32Array toInt32Array(const std::vector<int>& values) {
  return Int32Array(
      val::global("Int32Array")
          .new_(emscripten::typed_memory_view(values.size(), values.data())));
}

std::vector<double> toVector(const NumberArray& values) {
  if (values.isUndefined() || values.isNull()) return {};
  return emscripten::convertJSArrayToNumberVector<double>(values);
}

// JS-facing counterparts of the request structs, with number[] in place of
// std::vector<double>.
struct JsVolSpec {
  VolModel model = VolModel::kFlat;
  double vol = 0.0;
  NumberArray term_maturities{val::array()};
  NumberArray term_vols{val::array()};
  double smile_range = 0.0;
  double smile_stretch = 0.0;

  VolSpec toCpp() const {
    return {.model = model,
            .vol = vol,
            .term_maturities = toVector(term_maturities),
            .term_vols = toVector(term_vols),
            .smile_range = smile_range,
            .smile_stretch = smile_stretch};
  }
};

struct JsBinomialTreeSpec {
  TreeModel model = TreeModel::kCoxRossRubinstein;
  double spot = 0.0;
  JsVolSpec vol;
  double rate = 0.0;
  double drift = 0.0;
  double dt_years = 0.0;
};

struct JsVanillaPricingRequest {
  JsBinomialTreeSpec tree;
  VanillaOptionSpec option;

  VanillaPricingRequest toCpp() const {
    return {.tree = {.model = tree.model,
                     .spot = tree.spot,
                     .vol = tree.vol.toCpp(),
                     .rate = tree.rate,
                     .drift = tree.drift,
                     .dt_years = tree.dt_years},
            .option = option};
  }
};

struct JsRateCurveSpec {
  NumberArray maturities{val::array()};
  NumberArray rates{val::array()};

  RateCurveSpec toCpp() const {
    return {.maturities = toVector(maturities), .rates = toVector(rates)};
  }
};

struct JsFxTreeSpec {
  double spot = 0.0;
  double vol = 0.0;
  JsRateCurveSpec domestic;
  JsRateCurveSpec foreign;
  double dt_years = 0.0;
};

struct JsFxVanillaPricingRequest {
  JsFxTreeSpec tree;
  VanillaOptionSpec option;

  FxVanillaPricingRequest toCpp() const {
    return {.tree = {.spot = tree.spot,
                     .vol = tree.vol,
                     .domestic = tree.domestic.toCpp(),
                     .foreign = tree.foreign.toCpp(),
                     .dt_years = tree.dt_years},
            .option = option};
  }
};

// The JS-facing counterpart of VanillaPricingResult.
struct JsVanillaPricingResult {
  double price = 0.0;
  double bsm_price = 0.0;
  double bsm_vol = 0.0;
  int expiry_index = 0;
  Float64Array times{val::null()};
  Float64Array forward_dfs{val::null()};
  Float64Array forward_growth{val::null()};
  Float64Array asset_values{val::null()};
  Float64Array option_values{val::null()};
  Float64Array exercise_values{val::null()};
  Float64Array continuation_values{val::null()};
  Float64Array up_probs{val::null()};
  Float64Array arrow_debreu{val::null()};
};

JsVanillaPricingResult toJs(const VanillaPricingResult& result) {
  return {
      .price = result.price,
      .bsm_price = result.bsm_price,
      .bsm_vol = result.bsm_vol,
      .expiry_index = result.expiry_index,
      .times = toFloat64Array(result.times),
      .forward_dfs = toFloat64Array(result.forward_dfs),
      .forward_growth = toFloat64Array(result.forward_growth),
      .asset_values = toFloat64Array(result.asset_values),
      .option_values = toFloat64Array(result.option_values),
      .exercise_values = toFloat64Array(result.exercise_values),
      .continuation_values = toFloat64Array(result.continuation_values),
      .up_probs = toFloat64Array(result.up_probs),
      .arrow_debreu = toFloat64Array(result.arrow_debreu),
  };
}

struct JsHullWhiteTreeSpec {
  double mean_reversion = 0.0;
  double sigma = 0.0;
  double dt_years = 0.0;
  double horizon_years = 0.0;
  JsRateCurveSpec curve;
  double bond_face = 0.0;

  HullWhiteTreeSpec toCpp() const {
    return {.mean_reversion = mean_reversion,
            .sigma = sigma,
            .dt_years = dt_years,
            .horizon_years = horizon_years,
            .curve = curve.toCpp(),
            .bond_face = bond_face};
  }
};

// The JS-facing counterpart of HullWhiteTreeResult.
struct JsHullWhiteTreeResult {
  double dr = 0.0;
  int j_max = 0;
  double bond_price = 0.0;
  double market_bond_price = 0.0;
  Float64Array times{val::null()};
  Float64Array alphas{val::null()};
  Float64Array market_dfs{val::null()};
  Float64Array tree_dfs{val::null()};
  Int32Array slice_offsets{val::null()};
  Int32Array j{val::null()};
  Float64Array x{val::null()};
  Float64Array short_rates{val::null()};
  Float64Array pu{val::null()};
  Float64Array pm{val::null()};
  Float64Array pd{val::null()};
  Int32Array successor_mid{val::null()};
  Int32Array branch_style{val::null()};
  Float64Array arrow_debreu{val::null()};
  Float64Array bond_values{val::null()};
};

JsHullWhiteTreeResult toJs(const HullWhiteTreeResult& r) {
  return {
      .dr = r.dr,
      .j_max = r.j_max,
      .bond_price = r.bond_price,
      .market_bond_price = r.market_bond_price,
      .times = toFloat64Array(r.times),
      .alphas = toFloat64Array(r.alphas),
      .market_dfs = toFloat64Array(r.market_dfs),
      .tree_dfs = toFloat64Array(r.tree_dfs),
      .slice_offsets = toInt32Array(r.slice_offsets),
      .j = toInt32Array(r.j),
      .x = toFloat64Array(r.x),
      .short_rates = toFloat64Array(r.short_rates),
      .pu = toFloat64Array(r.pu),
      .pm = toFloat64Array(r.pm),
      .pd = toFloat64Array(r.pd),
      .successor_mid = toInt32Array(r.successor_mid),
      .branch_style = toInt32Array(r.branch_style),
      .arrow_debreu = toFloat64Array(r.arrow_debreu),
      .bond_values = toFloat64Array(r.bond_values),
  };
}

struct JsBondOptionRequest {
  JsHullWhiteTreeSpec tree;
  BondOptionSpec option;

  BondOptionRequest toCpp() const {
    return {.tree = tree.toCpp(), .option = option};
  }
};

// The JS-facing counterpart of BondOptionResult.
struct JsBondOptionResult {
  JsHullWhiteTreeResult tree;
  double price = 0.0;
  double european_price = 0.0;
  double analytic_price = 0.0;
  int expiry_index = 0;
  Float64Array option_values{val::null()};
  Float64Array exercise_values{val::null()};
  Float64Array continuation_values{val::null()};
};

JsBondOptionResult toJs(const BondOptionResult& r) {
  return {
      .tree = toJs(r.tree),
      .price = r.price,
      .european_price = r.european_price,
      .analytic_price = r.analytic_price,
      .expiry_index = r.expiry_index,
      .option_values = toFloat64Array(r.option_values),
      .exercise_values = toFloat64Array(r.exercise_values),
      .continuation_values = toFloat64Array(r.continuation_values),
  };
}

struct JsGreekSurfaceRequest {
  Greek greek = Greek::kGamma;
  double strike = 0.0;
  double vol = 0.0;
  double rate = 0.0;
  bool is_call = true;
  NumberArray spots{val::array()};
  NumberArray expiries{val::array()};

  GreekSurfaceRequest toCpp() const {
    return {.greek = greek,
            .strike = strike,
            .vol = vol,
            .rate = rate,
            .is_call = is_call,
            .spots = toVector(spots),
            .expiries = toVector(expiries)};
  }
};

// The seed is a plain number in JS (Embind would map uint64_t to a BigInt).
struct JsDeltaHedgeRequest {
  double spot = 0.0;
  double strike = 0.0;
  double expiry_years = 0.0;
  bool is_call = true;
  double implied_vol = 0.0;
  double realised_vol = 0.0;
  double rate = 0.0;
  int rebalances = 0;
  int paths = 0;
  double seed = 1;

  DeltaHedgeRequest toCpp() const {
    return {.spot = spot,
            .strike = strike,
            .expiry_years = expiry_years,
            .is_call = is_call,
            .implied_vol = implied_vol,
            .realised_vol = realised_vol,
            .rate = rate,
            .rebalances = rebalances,
            .paths = paths,
            .seed = static_cast<uint64_t>(seed)};
  }
};

struct JsDeltaHedgeResult {
  Float64Array pnl{val::null()};
  double mean = 0.0;
  double stddev = 0.0;
  double premium = 0.0;
  Float64Array times{val::null()};
  Float64Array spots{val::null()};
  Float64Array deltas{val::null()};
  Float64Array cumulative_pnl{val::null()};
  Float64Array cumulative_theta{val::null()};
  Float64Array cumulative_gamma{val::null()};
};

JsDeltaHedgeResult toJs(const DeltaHedgeResult& r) {
  return {
      .pnl = toFloat64Array(r.pnl),
      .mean = r.mean,
      .stddev = r.stddev,
      .premium = r.premium,
      .times = toFloat64Array(r.times),
      .spots = toFloat64Array(r.spots),
      .deltas = toFloat64Array(r.deltas),
      .cumulative_pnl = toFloat64Array(r.cumulative_pnl),
      .cumulative_theta = toFloat64Array(r.cumulative_theta),
      .cumulative_gamma = toFloat64Array(r.cumulative_gamma),
  };
}

struct JsSwaptionRequest {
  JsHullWhiteTreeSpec tree;
  SwaptionSpec swaption;
};

struct JsSwaptionResult {
  JsBondOptionResult option;
  double fixed_rate = 0.0;
  double par_rate = 0.0;
  double annuity = 0.0;
  Float64Array payment_times{val::null()};
  double normal_vol = 0.0;
  double analytic_normal_vol = 0.0;
};

JsSwaptionResult toJs(SwaptionResult r) {
  return {.option = toJs(r.option),
          .fixed_rate = r.fixed_rate,
          .par_rate = r.par_rate,
          .annuity = r.annuity,
          .payment_times = toFloat64Array(r.payment_times),
          .normal_vol = r.normal_vol,
          .analytic_normal_vol = r.analytic_normal_vol};
}

struct JsCapRequest {
  JsHullWhiteTreeSpec tree;
  CapSpec cap;
};

struct JsCapResult {
  double strike = 0.0;
  double price = 0.0;
  double analytic_price = 0.0;
  Float64Array fixing_times{val::null()};
  Float64Array payment_times{val::null()};
  Float64Array forward_rates{val::null()};
  Float64Array prices{val::null()};
  Float64Array analytic_prices{val::null()};
  Float64Array normal_vols{val::null()};
};

JsCapResult toJs(const CapResult& r) {
  return {.strike = r.strike,
          .price = r.price,
          .analytic_price = r.analytic_price,
          .fixing_times = toFloat64Array(r.fixing_times),
          .payment_times = toFloat64Array(r.payment_times),
          .forward_rates = toFloat64Array(r.forward_rates),
          .prices = toFloat64Array(r.prices),
          .analytic_prices = toFloat64Array(r.analytic_prices),
          .normal_vols = toFloat64Array(r.normal_vols)};
}

// Quotes arrive as a plain JS array of RateOptionQuote objects.
std::vector<RateOptionQuote> toQuotes(const QuoteArray& quotes) {
  std::vector<RateOptionQuote> out;
  if (quotes.isUndefined() || quotes.isNull()) return out;
  const int n = quotes["length"].as<int>();
  for (int i = 0; i < n; ++i) {
    const val q = quotes[i];
    out.push_back({.type = q["type"].as<RateOptionType>(),
                   .expiry = q["expiry"].as<double>(),
                   .tenor = q["tenor"].as<double>(),
                   .normal_vol = q["normalVol"].as<double>()});
  }
  return out;
}

struct JsCalibrationRequest {
  JsRateCurveSpec curve;
  QuoteArray quotes{val::array()};
  int fixed_payments_per_year = 1;
};

struct JsCalibrationResult {
  HullWhiteParams params;
  Float64Array market_vols{val::null()};
  Float64Array model_vols{val::null()};
  double rms_error = 0.0;
  int iterations = 0;
  bool converged = false;
};

JsCalibrationResult toJs(const CalibrationResult& r) {
  return {.params = r.params,
          .market_vols = toFloat64Array(r.market_vols),
          .model_vols = toFloat64Array(r.model_vols),
          .rms_error = r.rms_error,
          .iterations = r.iterations,
          .converged = r.converged};
}

struct JsModelVolsRequest {
  JsRateCurveSpec curve;
  HullWhiteParams params;
  QuoteArray quotes{val::array()};
  int fixed_payments_per_year = 1;
};

// Unwraps a StatusOr, converting its value with `convert`, or throws a JS
// Error with the status message.
template <typename T, typename ConvertFn>
auto valueOrThrow(absl::StatusOr<T> status_or, ConvertFn convert) {
  val error = val::undefined();
  {
    // Scoped so that C++ objects are destroyed before the JS throw, which
    // unwinds without running C++ destructors.
    const absl::StatusOr<T> result = std::move(status_or);
    if (result.ok()) return convert(*result);
    error = val::global("Error").new_(std::string(result.status().message()));
  }
  error.throw_();
}

JsVanillaPricingResult jsPriceVanilla(const JsVanillaPricingRequest& request) {
  return valueOrThrow(priceVanilla(request.toCpp()),
                      [](const VanillaPricingResult& r) { return toJs(r); });
}

JsVanillaPricingResult jsPriceFxVanilla(
    const JsFxVanillaPricingRequest& request) {
  return valueOrThrow(priceFxVanilla(request.toCpp()),
                      [](const VanillaPricingResult& r) { return toJs(r); });
}

JsHullWhiteTreeResult jsBuildHullWhiteTree(const JsHullWhiteTreeSpec& spec) {
  return valueOrThrow(buildHullWhiteTree(spec.toCpp()),
                      [](const HullWhiteTreeResult& r) { return toJs(r); });
}

JsBondOptionResult jsPriceBondOption(const JsBondOptionRequest& request) {
  return valueOrThrow(priceBondOption(request.toCpp()),
                      [](const BondOptionResult& r) { return toJs(r); });
}

GreeksResult jsComputeGreeks(const JsVanillaPricingRequest& request,
                             double vol_bump,
                             double rate_bump) {
  return valueOrThrow(computeGreeks(request.toCpp(), vol_bump, rate_bump),
                      [](const GreeksResult& r) { return r; });
}

Float64Array jsGreekSurface(const JsGreekSurfaceRequest& request) {
  return valueOrThrow(greekSurface(request.toCpp()), toFloat64Array);
}

JsDeltaHedgeResult jsSimulateDeltaHedge(const JsDeltaHedgeRequest& request) {
  return valueOrThrow(simulateDeltaHedge(request.toCpp()),
                      [](const DeltaHedgeResult& r) { return toJs(r); });
}

JsSwaptionResult jsPriceSwaption(const JsSwaptionRequest& request) {
  return valueOrThrow(priceSwaption({.tree = request.tree.toCpp(),
                                     .swaption = request.swaption}),
                      [](const SwaptionResult& r) { return toJs(r); });
}

JsCapResult jsPriceCap(const JsCapRequest& request) {
  return valueOrThrow(
      priceCap({.tree = request.tree.toCpp(), .cap = request.cap}),
      [](const CapResult& r) { return toJs(r); });
}

JsCalibrationResult jsCalibrateHullWhite(const JsCalibrationRequest& request) {
  return valueOrThrow(calibrateHullWhite({.curve = request.curve.toCpp(),
                                          .quotes = toQuotes(request.quotes),
                                          .fixed_payments_per_year =
                                              request.fixed_payments_per_year}),
                      [](const CalibrationResult& r) { return toJs(r); });
}

Float64Array jsModelNormalVols(const JsModelVolsRequest& request) {
  return valueOrThrow(modelNormalVols({.curve = request.curve.toCpp(),
                                       .params = request.params,
                                       .quotes = toQuotes(request.quotes),
                                       .fixed_payments_per_year =
                                           request.fixed_payments_per_year}),
                      toFloat64Array);
}

Float64Array jsEvaluateCurve(const JsRateCurveSpec& curve,
                             const NumberArray& times) {
  return valueOrThrow(evaluateCurve(curve.toCpp(), toVector(times)),
                      toFloat64Array);
}

Float64Array jsEvaluateVol(const JsVolSpec& vol,
                           double spot,
                           const NumberArray& xs) {
  return valueOrThrow(evaluateVol(vol.toCpp(), spot, toVector(xs)),
                      toFloat64Array);
}

double jsImpliedVolatility(const ImpliedVolRequest& request) {
  return valueOrThrow(impliedVolatility(request), [](double v) { return v; });
}

}  // namespace
}  // namespace smileexplorer::api

EMSCRIPTEN_BINDINGS(smileexplorer) {
  using namespace smileexplorer::api;
  using smileexplorer::HullWhiteParams;
  using smileexplorer::RateOptionQuote;
  using smileexplorer::RateOptionType;
  namespace em = emscripten;

  em::register_type<Float64Array>("Float64Array");
  em::register_type<Int32Array>("Int32Array");
  em::register_type<NumberArray>("number[]");
  em::register_type<QuoteArray>("RateOptionQuote[]");

  em::enum_<TreeModel>("TreeModel")
      .value("CoxRossRubinstein", TreeModel::kCoxRossRubinstein)
      .value("JarrowRudd", TreeModel::kJarrowRudd)
      .value("LocalVolatility", TreeModel::kLocalVolatility);

  em::enum_<VolModel>("VolModel")
      .value("Flat", VolModel::kFlat)
      .value("TermStructure", VolModel::kTermStructure)
      .value("SigmoidSmile", VolModel::kSigmoidSmile);

  em::value_object<JsVolSpec>("VolSpec")
      .field("model", &JsVolSpec::model)
      .field("vol", &JsVolSpec::vol)
      .field("termMaturities", &JsVolSpec::term_maturities)
      .field("termVols", &JsVolSpec::term_vols)
      .field("smileRange", &JsVolSpec::smile_range)
      .field("smileStretch", &JsVolSpec::smile_stretch);

  em::value_object<JsBinomialTreeSpec>("BinomialTreeSpec")
      .field("model", &JsBinomialTreeSpec::model)
      .field("spot", &JsBinomialTreeSpec::spot)
      .field("vol", &JsBinomialTreeSpec::vol)
      .field("rate", &JsBinomialTreeSpec::rate)
      .field("drift", &JsBinomialTreeSpec::drift)
      .field("dtYears", &JsBinomialTreeSpec::dt_years);

  em::value_object<VanillaOptionSpec>("VanillaOptionSpec")
      .field("strike", &VanillaOptionSpec::strike)
      .field("expiryYears", &VanillaOptionSpec::expiry_years)
      .field("isCall", &VanillaOptionSpec::is_call)
      .field("isAmerican", &VanillaOptionSpec::is_american);

  em::value_object<JsVanillaPricingRequest>("VanillaPricingRequest")
      .field("tree", &JsVanillaPricingRequest::tree)
      .field("option", &JsVanillaPricingRequest::option);

  em::value_object<JsRateCurveSpec>("RateCurveSpec")
      .field("maturities", &JsRateCurveSpec::maturities)
      .field("rates", &JsRateCurveSpec::rates);

  em::value_object<JsFxTreeSpec>("FxTreeSpec")
      .field("spot", &JsFxTreeSpec::spot)
      .field("vol", &JsFxTreeSpec::vol)
      .field("domestic", &JsFxTreeSpec::domestic)
      .field("foreign", &JsFxTreeSpec::foreign)
      .field("dtYears", &JsFxTreeSpec::dt_years);

  em::value_object<JsFxVanillaPricingRequest>("FxVanillaPricingRequest")
      .field("tree", &JsFxVanillaPricingRequest::tree)
      .field("option", &JsFxVanillaPricingRequest::option);

  em::value_object<ImpliedVolRequest>("ImpliedVolRequest")
      .field("price", &ImpliedVolRequest::price)
      .field("spot", &ImpliedVolRequest::spot)
      .field("strike", &ImpliedVolRequest::strike)
      .field("expiryYears", &ImpliedVolRequest::expiry_years)
      .field("rate", &ImpliedVolRequest::rate)
      .field("isCall", &ImpliedVolRequest::is_call);

  em::value_object<JsVanillaPricingResult>("VanillaPricingResult")
      .field("price", &JsVanillaPricingResult::price)
      .field("bsmPrice", &JsVanillaPricingResult::bsm_price)
      .field("bsmVol", &JsVanillaPricingResult::bsm_vol)
      .field("expiryIndex", &JsVanillaPricingResult::expiry_index)
      .field("times", &JsVanillaPricingResult::times)
      .field("forwardDfs", &JsVanillaPricingResult::forward_dfs)
      .field("forwardGrowth", &JsVanillaPricingResult::forward_growth)
      .field("assetValues", &JsVanillaPricingResult::asset_values)
      .field("optionValues", &JsVanillaPricingResult::option_values)
      .field("exerciseValues", &JsVanillaPricingResult::exercise_values)
      .field("continuationValues", &JsVanillaPricingResult::continuation_values)
      .field("upProbs", &JsVanillaPricingResult::up_probs)
      .field("arrowDebreu", &JsVanillaPricingResult::arrow_debreu);

  em::value_object<JsHullWhiteTreeSpec>("HullWhiteTreeSpec")
      .field("meanReversion", &JsHullWhiteTreeSpec::mean_reversion)
      .field("sigma", &JsHullWhiteTreeSpec::sigma)
      .field("dtYears", &JsHullWhiteTreeSpec::dt_years)
      .field("horizonYears", &JsHullWhiteTreeSpec::horizon_years)
      .field("curve", &JsHullWhiteTreeSpec::curve)
      .field("bondFace", &JsHullWhiteTreeSpec::bond_face);

  em::value_object<JsHullWhiteTreeResult>("HullWhiteTreeResult")
      .field("dr", &JsHullWhiteTreeResult::dr)
      .field("jMax", &JsHullWhiteTreeResult::j_max)
      .field("bondPrice", &JsHullWhiteTreeResult::bond_price)
      .field("marketBondPrice", &JsHullWhiteTreeResult::market_bond_price)
      .field("times", &JsHullWhiteTreeResult::times)
      .field("alphas", &JsHullWhiteTreeResult::alphas)
      .field("marketDfs", &JsHullWhiteTreeResult::market_dfs)
      .field("treeDfs", &JsHullWhiteTreeResult::tree_dfs)
      .field("sliceOffsets", &JsHullWhiteTreeResult::slice_offsets)
      .field("j", &JsHullWhiteTreeResult::j)
      .field("x", &JsHullWhiteTreeResult::x)
      .field("shortRates", &JsHullWhiteTreeResult::short_rates)
      .field("pu", &JsHullWhiteTreeResult::pu)
      .field("pm", &JsHullWhiteTreeResult::pm)
      .field("pd", &JsHullWhiteTreeResult::pd)
      .field("successorMid", &JsHullWhiteTreeResult::successor_mid)
      .field("branchStyle", &JsHullWhiteTreeResult::branch_style)
      .field("arrowDebreu", &JsHullWhiteTreeResult::arrow_debreu)
      .field("bondValues", &JsHullWhiteTreeResult::bond_values);

  em::value_object<BondOptionSpec>("BondOptionSpec")
      .field("strike", &BondOptionSpec::strike)
      .field("expiryYears", &BondOptionSpec::expiry_years)
      .field("isCall", &BondOptionSpec::is_call)
      .field("isAmerican", &BondOptionSpec::is_american);

  em::value_object<JsBondOptionRequest>("BondOptionRequest")
      .field("tree", &JsBondOptionRequest::tree)
      .field("option", &JsBondOptionRequest::option);

  em::value_object<JsBondOptionResult>("BondOptionResult")
      .field("tree", &JsBondOptionResult::tree)
      .field("price", &JsBondOptionResult::price)
      .field("europeanPrice", &JsBondOptionResult::european_price)
      .field("analyticPrice", &JsBondOptionResult::analytic_price)
      .field("expiryIndex", &JsBondOptionResult::expiry_index)
      .field("optionValues", &JsBondOptionResult::option_values)
      .field("exerciseValues", &JsBondOptionResult::exercise_values)
      .field("continuationValues", &JsBondOptionResult::continuation_values);

  em::enum_<Greek>("Greek")
      .value("Price", Greek::kPrice)
      .value("Delta", Greek::kDelta)
      .value("Gamma", Greek::kGamma)
      .value("Theta", Greek::kTheta)
      .value("Vega", Greek::kVega)
      .value("Vanna", Greek::kVanna)
      .value("Volga", Greek::kVolga)
      .value("Charm", Greek::kCharm)
      .value("Colour", Greek::kColour);

  em::value_object<Greeks>("Greeks")
      .field("delta", &Greeks::delta)
      .field("gamma", &Greeks::gamma)
      .field("theta", &Greeks::theta)
      .field("vega", &Greeks::vega)
      .field("rho", &Greeks::rho);

  em::value_object<GreeksResult>("GreeksResult")
      .field("price", &GreeksResult::price)
      .field("tree", &GreeksResult::tree)
      .field("closedForm", &GreeksResult::closed_form)
      .field("vanna", &GreeksResult::vanna)
      .field("volga", &GreeksResult::volga)
      .field("charm", &GreeksResult::charm)
      .field("colour", &GreeksResult::colour)
      .field("volBump", &GreeksResult::vol_bump)
      .field("rateBump", &GreeksResult::rate_bump);

  em::value_object<JsGreekSurfaceRequest>("GreekSurfaceRequest")
      .field("greek", &JsGreekSurfaceRequest::greek)
      .field("strike", &JsGreekSurfaceRequest::strike)
      .field("vol", &JsGreekSurfaceRequest::vol)
      .field("rate", &JsGreekSurfaceRequest::rate)
      .field("isCall", &JsGreekSurfaceRequest::is_call)
      .field("spots", &JsGreekSurfaceRequest::spots)
      .field("expiries", &JsGreekSurfaceRequest::expiries);

  em::value_object<JsDeltaHedgeRequest>("DeltaHedgeRequest")
      .field("spot", &JsDeltaHedgeRequest::spot)
      .field("strike", &JsDeltaHedgeRequest::strike)
      .field("expiryYears", &JsDeltaHedgeRequest::expiry_years)
      .field("isCall", &JsDeltaHedgeRequest::is_call)
      .field("impliedVol", &JsDeltaHedgeRequest::implied_vol)
      .field("realisedVol", &JsDeltaHedgeRequest::realised_vol)
      .field("rate", &JsDeltaHedgeRequest::rate)
      .field("rebalances", &JsDeltaHedgeRequest::rebalances)
      .field("paths", &JsDeltaHedgeRequest::paths)
      .field("seed", &JsDeltaHedgeRequest::seed);

  em::value_object<JsDeltaHedgeResult>("DeltaHedgeResult")
      .field("pnl", &JsDeltaHedgeResult::pnl)
      .field("mean", &JsDeltaHedgeResult::mean)
      .field("stddev", &JsDeltaHedgeResult::stddev)
      .field("premium", &JsDeltaHedgeResult::premium)
      .field("times", &JsDeltaHedgeResult::times)
      .field("spots", &JsDeltaHedgeResult::spots)
      .field("deltas", &JsDeltaHedgeResult::deltas)
      .field("cumulativePnl", &JsDeltaHedgeResult::cumulative_pnl)
      .field("cumulativeTheta", &JsDeltaHedgeResult::cumulative_theta)
      .field("cumulativeGamma", &JsDeltaHedgeResult::cumulative_gamma);

  em::function("computeGreeks", &jsComputeGreeks);
  em::function("greekSurface", &jsGreekSurface);
  em::function("simulateDeltaHedge", &jsSimulateDeltaHedge);
  em::enum_<RateOptionType>("RateOptionType")
      .value("Caplet", RateOptionType::kCaplet)
      .value("Swaption", RateOptionType::kSwaption);

  em::value_object<RateOptionQuote>("RateOptionQuote")
      .field("type", &RateOptionQuote::type)
      .field("expiry", &RateOptionQuote::expiry)
      .field("tenor", &RateOptionQuote::tenor)
      .field("normalVol", &RateOptionQuote::normal_vol);

  em::value_object<HullWhiteParams>("HullWhiteParams")
      .field("meanReversion", &HullWhiteParams::mean_reversion)
      .field("sigma", &HullWhiteParams::sigma);

  em::value_object<SwaptionSpec>("SwaptionSpec")
      .field("expiryYears", &SwaptionSpec::expiry_years)
      .field("tenorYears", &SwaptionSpec::tenor_years)
      .field("fixedPaymentsPerYear", &SwaptionSpec::fixed_payments_per_year)
      .field("atTheMoney", &SwaptionSpec::at_the_money)
      .field("fixedRate", &SwaptionSpec::fixed_rate)
      .field("notional", &SwaptionSpec::notional)
      .field("isPayer", &SwaptionSpec::is_payer);

  em::value_object<JsSwaptionRequest>("SwaptionRequest")
      .field("tree", &JsSwaptionRequest::tree)
      .field("swaption", &JsSwaptionRequest::swaption);

  em::value_object<JsSwaptionResult>("SwaptionResult")
      .field("option", &JsSwaptionResult::option)
      .field("fixedRate", &JsSwaptionResult::fixed_rate)
      .field("parRate", &JsSwaptionResult::par_rate)
      .field("annuity", &JsSwaptionResult::annuity)
      .field("paymentTimes", &JsSwaptionResult::payment_times)
      .field("normalVol", &JsSwaptionResult::normal_vol)
      .field("analyticNormalVol", &JsSwaptionResult::analytic_normal_vol);

  em::value_object<CapSpec>("CapSpec")
      .field("maturityYears", &CapSpec::maturity_years)
      .field("paymentsPerYear", &CapSpec::payments_per_year)
      .field("atTheMoney", &CapSpec::at_the_money)
      .field("strike", &CapSpec::strike)
      .field("notional", &CapSpec::notional)
      .field("isCap", &CapSpec::is_cap);

  em::value_object<JsCapRequest>("CapRequest")
      .field("tree", &JsCapRequest::tree)
      .field("cap", &JsCapRequest::cap);

  em::value_object<JsCapResult>("CapResult")
      .field("strike", &JsCapResult::strike)
      .field("price", &JsCapResult::price)
      .field("analyticPrice", &JsCapResult::analytic_price)
      .field("fixingTimes", &JsCapResult::fixing_times)
      .field("paymentTimes", &JsCapResult::payment_times)
      .field("forwardRates", &JsCapResult::forward_rates)
      .field("prices", &JsCapResult::prices)
      .field("analyticPrices", &JsCapResult::analytic_prices)
      .field("normalVols", &JsCapResult::normal_vols);

  em::value_object<JsCalibrationRequest>("CalibrationRequest")
      .field("curve", &JsCalibrationRequest::curve)
      .field("quotes", &JsCalibrationRequest::quotes)
      .field("fixedPaymentsPerYear",
             &JsCalibrationRequest::fixed_payments_per_year);

  em::value_object<JsCalibrationResult>("CalibrationResult")
      .field("params", &JsCalibrationResult::params)
      .field("marketVols", &JsCalibrationResult::market_vols)
      .field("modelVols", &JsCalibrationResult::model_vols)
      .field("rmsError", &JsCalibrationResult::rms_error)
      .field("iterations", &JsCalibrationResult::iterations)
      .field("converged", &JsCalibrationResult::converged);

  em::value_object<JsModelVolsRequest>("ModelVolsRequest")
      .field("curve", &JsModelVolsRequest::curve)
      .field("params", &JsModelVolsRequest::params)
      .field("quotes", &JsModelVolsRequest::quotes)
      .field("fixedPaymentsPerYear",
             &JsModelVolsRequest::fixed_payments_per_year);

  em::function("priceSwaption", &jsPriceSwaption);
  em::function("priceCap", &jsPriceCap);
  em::function("calibrateHullWhite", &jsCalibrateHullWhite);
  em::function("modelNormalVols", &jsModelNormalVols);
  em::function("priceVanilla", &jsPriceVanilla);
  em::function("priceBondOption", &jsPriceBondOption);
  em::function("buildHullWhiteTree", &jsBuildHullWhiteTree);
  em::function("priceFxVanilla", &jsPriceFxVanilla);
  em::function("evaluateCurve", &jsEvaluateCurve);
  em::function("evaluateVol", &jsEvaluateVol);
  em::function("impliedVolatility", &jsImpliedVolatility);
}
