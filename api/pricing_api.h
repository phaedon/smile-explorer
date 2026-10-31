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

enum class TreeModel {
  kCoxRossRubinstein,
  kJarrowRudd,
  // Derman-Kani implied tree: node spacing follows a local volatility that
  // depends on the asset price. Requires a flat or sigmoid-smile VolSpec.
  kLocalVolatility,
};

enum class VolModel {
  kFlat,
  // Forward vols implied by a term structure of implied vols. Requires the
  // Cox-Ross-Rubinstein or Jarrow-Rudd tree model.
  kTermStructure,
  // A local volatility skew in the asset price. Requires the local volatility
  // tree model.
  kSigmoidSmile,
  // Local volatility from an SSVI implied-vol surface, by Dupire's formula
  // (see api/smile_api.h to fit one to quotes). Requires the local
  // volatility tree model.
  kSsviLocalVol,
};

// Annualised lognormal volatilities, as decimals (0.16 == 16%).
struct VolSpec {
  VolModel model = VolModel::kFlat;
  // The flat volatility; for kSigmoidSmile, the level approached as the asset
  // price rises well above spot.
  double vol = 0.16;
  // kTermStructure: implied vols at strictly increasing maturities (in years).
  std::vector<double> term_maturities;
  std::vector<double> term_vols;
  // kSigmoidSmile:
  //   sigma(S) = vol + smile_range / (1 + exp(smile_stretch * (S - spot)))
  double smile_range = 0.0;
  double smile_stretch = 0.0;
  // kSsviLocalVol: the at-the-money implied vols are term_vols at
  // term_maturities, and these are SSVI's other parameters (rho in (-1, 1),
  // eta > 0, gamma in (0, 1)). `vol` must still be positive but is unused.
  double ssvi_rho = 0.0;
  double ssvi_eta = 0.0;
  double ssvi_gamma = 0.0;
};

// A recombining binomial tree for a single asset, discounted on a flat curve.
struct BinomialTreeSpec {
  TreeModel model = TreeModel::kCoxRossRubinstein;
  double spot = 100.0;
  VolSpec vol;
  // Flat, continuously-compounded discount rate, as a decimal.
  double rate = 0.04;
  // Expected drift of the asset (Jarrow-Rudd only), as a decimal.
  double drift = 0.1;
  // Size of each timestep, in years. With a term structure of vol, this is
  // the first timestep; later ones shrink as vol rises (and grow as it falls)
  // so that the tree recombines.
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

// A zero curve: continuously-compounded zero rates (as decimals) at strictly
// increasing maturities (in years), with constant forward rates between them.
// Beyond the last maturity, the last forward rate continues.
struct RateCurveSpec {
  std::vector<double> maturities;
  std::vector<double> rates;
};

// A Cox-Ross-Rubinstein tree for an exchange rate quoted FOR-DOM (the price of
// one unit of the foreign currency, in the domestic currency), with flat vol.
// Holding the foreign currency earns the foreign rate, so the tree's
// risk-neutral drift over each step is the domestic forward rate less the
// foreign one.
struct FxTreeSpec {
  double spot = 1.0;
  double vol = 0.1;
  RateCurveSpec domestic;
  RateCurveSpec foreign;
  double dt_years = 1 / 52.;
};

struct FxVanillaPricingRequest {
  FxTreeSpec tree;
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
  // The volatility used for bsm_price: the flat vol, the implied vol to
  // expiry for a term structure, or the local vol at spot for a smile.
  double bsm_vol = 0.0;

  // Time index of the expiry: the tree covers time indices 0..expiry_index.
  // The requested expiry is snapped to the nearest point on the timegrid.
  int expiry_index = 0;

  // Per time index (size expiry_index + 1): time in years.
  std::vector<double> times;
  // Per time index: discount factor from times[t] to times[t + 1]. The final
  // entry (at expiry) is NaN.
  std::vector<double> forward_dfs;
  // Per time index: the risk-neutral expected growth of the underlying from
  // times[t] to times[t + 1], so that
  //   up_prob = (forward_growth - down_ratio) / (up_ratio - down_ratio).
  // This is 1 / forward_df, or for an exchange rate, the domestic 1 /
  // forward_df divided by the foreign one. NaN at expiry.
  std::vector<double> forward_growth;

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

// Prices a European or American option on an exchange rate. The result's
// bsm_price is the Garman-Kohlhagen price and bsm_vol the (flat) vol.
absl::StatusOr<VanillaPricingResult> priceFxVanilla(
    const FxVanillaPricingRequest& request);

// Discount factors of the curve at each time in `times` (in years).
absl::StatusOr<std::vector<double>> evaluateCurve(
    const RateCurveSpec& curve, const std::vector<double>& times);

// Evaluates the forward (kFlat, kTermStructure) or local (kSigmoidSmile)
// volatility at each point in `xs`: times in years for a term structure, or
// asset prices for a smile.
absl::StatusOr<std::vector<double>> evaluateVol(const VolSpec& vol,
                                                double spot,
                                                const std::vector<double>& xs);

struct ImpliedVolRequest {
  double price = 0.0;
  double spot = 100.0;
  double strike = 100.0;
  double expiry_years = 1.0;
  // Flat, continuously-compounded discount rate, as a decimal.
  double rate = 0.04;
  bool is_call = true;
};

// The Black-Scholes implied volatility of a European option price. Fails if
// the price is outside the no-arbitrage bounds.
absl::StatusOr<double> impliedVolatility(const ImpliedVolRequest& request);

}  // namespace smileexplorer::api

#endif  // SMILEEXPLORER_API_PRICING_API_H_
