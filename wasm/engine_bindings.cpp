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
#include <vector>

#include "absl/status/statusor.h"
#include "api/pricing_api.h"

namespace smileexplorer::api {
namespace {

using emscripten::val;

EMSCRIPTEN_DECLARE_VAL_TYPE(Float64Array);

Float64Array toFloat64Array(const std::vector<double>& values) {
  // Constructing a Float64Array from a view copies it out of wasm memory.
  return Float64Array(val::global("Float64Array")
                          .new_(emscripten::typed_memory_view(values.size(),
                                                              values.data())));
}

// The JS-facing counterpart of VanillaPricingResult.
struct JsVanillaPricingResult {
  double price = 0.0;
  double bsm_price = 0.0;
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

// Throws a JS Error (with the status message) for invalid requests.
JsVanillaPricingResult jsPriceVanilla(const VanillaPricingRequest& request) {
  val error = val::undefined();
  {
    // Scoped so that the C++ result is destroyed before the JS throw, which
    // unwinds without running C++ destructors.
    const absl::StatusOr<VanillaPricingResult> result = priceVanilla(request);
    if (result.ok()) return toJs(*result);
    error = val::global("Error").new_(std::string(result.status().message()));
  }
  error.throw_();
}

}  // namespace
}  // namespace smileexplorer::api

EMSCRIPTEN_BINDINGS(smileexplorer) {
  using namespace smileexplorer::api;
  namespace em = emscripten;

  em::register_type<Float64Array>("Float64Array");

  em::enum_<TreeModel>("TreeModel")
      .value("CoxRossRubinstein", TreeModel::kCoxRossRubinstein)
      .value("JarrowRudd", TreeModel::kJarrowRudd);

  em::value_object<BinomialTreeSpec>("BinomialTreeSpec")
      .field("model", &BinomialTreeSpec::model)
      .field("spot", &BinomialTreeSpec::spot)
      .field("vol", &BinomialTreeSpec::vol)
      .field("rate", &BinomialTreeSpec::rate)
      .field("drift", &BinomialTreeSpec::drift)
      .field("dtYears", &BinomialTreeSpec::dt_years);

  em::value_object<VanillaOptionSpec>("VanillaOptionSpec")
      .field("strike", &VanillaOptionSpec::strike)
      .field("expiryYears", &VanillaOptionSpec::expiry_years)
      .field("isCall", &VanillaOptionSpec::is_call)
      .field("isAmerican", &VanillaOptionSpec::is_american);

  em::value_object<VanillaPricingRequest>("VanillaPricingRequest")
      .field("tree", &VanillaPricingRequest::tree)
      .field("option", &VanillaPricingRequest::option);

  em::value_object<JsVanillaPricingResult>("VanillaPricingResult")
      .field("price", &JsVanillaPricingResult::price)
      .field("bsmPrice", &JsVanillaPricingResult::bsm_price)
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
}
