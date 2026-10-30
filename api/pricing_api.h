#ifndef SMILEEXPLORER_API_PRICING_API_H_
#define SMILEEXPLORER_API_PRICING_API_H_

#include <vector>

#include "absl/status/statusor.h"

// A runtime-configurable entry point to the pricing library.
//
// The core library composes models at compile time (for example
// StochasticTreeModel<CRRPropagator>). This layer takes plain request structs,
// validates them, picks the model at runtime, and returns everything needed to
// explain the result -- not just the price, but every node of the backward
// induction. It is the layer that the WebAssembly bindings expose to JS.

namespace smileexplorer::api {

enum class TreeModel { kCoxRossRubinstein, kJarrowRudd };

// A recombining binomial tree for a single asset with flat vol, discounted on
// a flat curve.
struct BinomialTreeSpec {
  TreeModel model = TreeModel::kCoxRossRubinstein;
  double spot = 100.0;
  // Annualised lognormal volatility, as a decimal (0.16 == 16%).
  double vol = 0.16;
  // Flat, continuously-compounded discount rate, as a decimal.
  double rate = 0.04;
  // Expected drift of the asset (Jarrow-Rudd only), as a decimal.
  double drift = 0.1;
  // Size of each timestep, in years.
  double dt_years = 1 / 52.;
};

struct VanillaOptionSpec {
  double strike = 100.0;
  double expiry_years = 1.0;
  bool is_call = true;
  bool is_american = false;
};

struct VanillaPricingRequest {
  BinomialTreeSpec tree;
  VanillaOptionSpec option;
};

// Index of node (t, i) in the triangular per-node arrays below, where t is the
// time index and 0 <= i <= t counts the states from the lowest one upwards.
inline int nodeIndex(int t, int i) { return t * (t + 1) / 2 + i; }

inline int numNodes(int num_time_indices) {
  return nodeIndex(num_time_indices, 0);
}

struct VanillaPricingResult {
  double price = 0.0;
  // The closed-form (Black-Scholes) European price, for reference. For an
  // American option, this is the price of its European counterpart.
  double bsm_price = 0.0;

  // Time index of the expiry: the tree covers time indices 0..expiry_index.
  // The requested expiry is snapped to the nearest point on the timegrid.
  int expiry_index = 0;

  // Per time index (size expiry_index + 1): time in years.
  std::vector<double> times;
  // Per time index: discount factor from times[t] to times[t + 1]. The final
  // entry (at expiry) is NaN.
  std::vector<double> forward_dfs;

  // Per node, indexed by nodeIndex(t, i):
  std::vector<double> asset_values;
  // Value of the option at the node (from backward induction).
  std::vector<double> option_values;
  // Payoff if the option were exercised at the node.
  std::vector<double> exercise_values;
  // Discounted expected value of holding, i.e.
  //   forward_df * (up_prob * V(t+1, i+1) + (1 - up_prob) * V(t+1, i)).
  // NaN at expiry.
  std::vector<double> continuation_values;
  // Risk-neutral probability of moving from (t, i) to (t+1, i+1). NaN at
  // expiry.
  std::vector<double> up_probs;
  // Arrow-Debreu price of each node: the discounted risk-neutral probability
  // of reaching it.
  std::vector<double> arrow_debreu;
};

absl::StatusOr<VanillaPricingResult> priceVanilla(
    const VanillaPricingRequest& request);

}  // namespace smileexplorer::api

#endif  // SMILEEXPLORER_API_PRICING_API_H_
