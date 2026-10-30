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

namespace smileexplorer::api {
namespace {

using emscripten::val;

EMSCRIPTEN_DECLARE_VAL_TYPE(Float64Array);
EMSCRIPTEN_DECLARE_VAL_TYPE(NumberArray);

Float64Array toFloat64Array(const std::vector<double>& values) {
  // Constructing a Float64Array from a view copies it out of wasm memory.
  return Float64Array(val::global("Float64Array")
                          .new_(emscripten::typed_memory_view(values.size(),
                                                              values.data())));
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

// The JS-facing counterpart of VanillaPricingResult.
struct JsVanillaPricingResult {
  double price = 0.0;
  double bsm_price = 0.0;
  double bsm_vol = 0.0;
  int expiry_index = 0;
  Float64Array times{val::null()};
  Float64Array forward_dfs{val::null()};
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
      .asset_values = toFloat64Array(result.asset_values),
      .option_values = toFloat64Array(result.option_values),
      .exercise_values = toFloat64Array(result.exercise_values),
      .continuation_values = toFloat64Array(result.continuation_values),
      .up_probs = toFloat64Array(result.up_probs),
      .arrow_debreu = toFloat64Array(result.arrow_debreu),
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
  return valueOrThrow(priceVanilla(request.toCpp()), toJs);
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
      .field("assetValues", &JsVanillaPricingResult::asset_values)
      .field("optionValues", &JsVanillaPricingResult::option_values)
      .field("exerciseValues", &JsVanillaPricingResult::exercise_values)
      .field("continuationValues",
             &JsVanillaPricingResult::continuation_values)
      .field("upProbs", &JsVanillaPricingResult::up_probs)
      .field("arrowDebreu", &JsVanillaPricingResult::arrow_debreu);

  em::function("priceVanilla", &jsPriceVanilla);
  em::function("evaluateVol", &jsEvaluateVol);
  em::function("impliedVolatility", &jsImpliedVolatility);
}
