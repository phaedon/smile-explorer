// JavaScript bindings (via Embind) for the pricing core, compiled to
// WebAssembly by //wasm:engine.
//
// This is a deliberately thin first binding: a single entry point that builds a
// CRR tree with flat vol and a flat discount curve, and prices a vanilla option
// on it. It returns plain JS values so that a browser UI can draw the tree.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cmath>

#include "derivatives/derivative.h"
#include "derivatives/vanilla_option.h"
#include "rates/zero_curve.h"
#include "trees/binomial_tree.h"
#include "trees/propagators.h"
#include "trees/stochastic_tree_model.h"
#include "volatility/volatility.h"

namespace smileexplorer {
namespace {

using emscripten::val;

// Guards against requests that would allocate a huge (dense N x N) tree.
constexpr int kMaxTimesteps = 5000;

val errorResult(const std::string& message) {
  val result = val::object();
  result.set("error", message);
  return result;
}

val priceVanillaCRR(double spot,
                    double vol,
                    double rate,
                    double tree_years,
                    double dt_years,
                    double strike,
                    double expiry_years,
                    bool is_call,
                    bool is_american) {
  if (spot <= 0 || vol <= 0 || strike <= 0) {
    return errorResult("spot, vol and strike must be positive");
  }
  if (dt_years <= 0 || tree_years <= 0) {
    return errorResult("tree_years and dt_years must be positive");
  }
  if (tree_years / dt_years > kMaxTimesteps) {
    return errorResult("too many timesteps (max " +
                       std::to_string(kMaxTimesteps) + ")");
  }
  if (expiry_years <= 0 || expiry_years >= tree_years) {
    return errorResult("expiry_years must be in (0, tree_years)");
  }

  StochasticTreeModel<CRRPropagator> asset(BinomialTree(tree_years, dt_years),
                                           CRRPropagator(spot));
  asset.forwardPropagate(Volatility(FlatVol(vol)));

  ZeroSpotCurve curve({1.0, 10.0}, {rate, rate});
  SingleAssetDerivative deriv(&asset.binomialTree(), &curve);

  const OptionPayoff payoff = is_call ? OptionPayoff::Call : OptionPayoff::Put;
  const VanillaOption option(
      strike,
      payoff,
      is_american ? ExerciseStyle::American : ExerciseStyle::European);
  const VanillaOption european(strike, payoff, ExerciseStyle::European);

  const BinomialTree& tree = asset.binomialTree();
  val times = val::array();
  val nodes = val::array();
  for (int t = 0; t < tree.numTimesteps(); ++t) {
    times.call<void>("push", tree.totalTimeAtIndex(t));
    nodes.call<void>("push", val::array(tree.statesAtTimeIndex(t)));
  }

  val result = val::object();
  result.set("price", deriv.price(option, expiry_years));
  // Closed-form European price, for comparison (even for American options).
  result.set("bsmPrice",
             european.blackScholes(spot, vol, expiry_years, rate, 0));
  result.set("times", times);
  result.set("nodes", nodes);
  return result;
}

}  // namespace
}  // namespace smileexplorer

EMSCRIPTEN_BINDINGS(smileexplorer) {
  emscripten::function("priceVanillaCRR", &smileexplorer::priceVanillaCRR);
}
