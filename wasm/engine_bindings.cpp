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
#include "api/pricing_api.h"
#include "api/short_rate_api.h"

namespace smileexplorer::api {
namespace {

using emscripten::val;

EMSCRIPTEN_DECLARE_VAL_TYPE(Float64Array);
EMSCRIPTEN_DECLARE_VAL_TYPE(Int32Array);
EMSCRIPTEN_DECLARE_VAL_TYPE(NumberArray);

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
  namespace em = emscripten;

  em::register_type<Float64Array>("Float64Array");
  em::register_type<Int32Array>("Int32Array");
  em::register_type<NumberArray>("number[]");

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

  em::function("priceVanilla", &jsPriceVanilla);
  em::function("buildHullWhiteTree", &jsBuildHullWhiteTree);
  em::function("priceFxVanilla", &jsPriceFxVanilla);
  em::function("evaluateCurve", &jsEvaluateCurve);
  em::function("evaluateVol", &jsEvaluateVol);
  em::function("impliedVolatility", &jsImpliedVolatility);
}
