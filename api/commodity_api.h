#ifndef SMILEEXPLORER_API_COMMODITY_API_H_
#define SMILEEXPLORER_API_COMMODITY_API_H_

#include <vector>

#include "absl/status/statusor.h"

// The runtime API for commodities: a mean-reverting trinomial tree for the
// spot price, fitted to today's futures curve, and options on futures priced
// on it.

namespace smileexplorer::api {

struct CommoditySpec {
  double spot = 3.0;
  // Futures pillars: delivery times in years (positive and increasing), and
  // their prices. Between pillars the curve is linear in the log of the price.
  std::vector<double> futures_times;
  std::vector<double> futures_prices;
  // Flat, continuously compounded rate for discounting options.
  double rate = 0.04;
  // ln S = X + alpha(t), with dX = -a X dt + sigma dz.
  double mean_reversion = 1.5;
  double sigma = 0.6;
  int steps_per_month = 4;
  // The tree covers [0, horizon_years], rounded up to a whole number of steps.
  double horizon_years = 1.0;
};

// As with HullWhiteTreeResult, per-node data are ragged: the nodes of slice t
// are at indices slice_offsets[t] .. slice_offsets[t + 1] - 1, ordered from
// the lowest price upwards.
struct CommodityTreeResult {
  // Spacing between neighbouring values of X: sigma * sqrt(3 dt).
  double dx = 0.0;
  int j_max = 0;

  // Per time slice:
  std::vector<double> times;
  std::vector<double> alphas;
  // The futures curve, and the tree's expected spot (which the fit makes
  // equal to it).
  std::vector<double> curve_prices;
  std::vector<double> expected_spots;
  // 5%, 50% and 95% quantiles of the spot in the continuous model, and under
  // geometric Brownian motion with the same sigma and the same forwards.
  std::vector<double> spot_q05;
  std::vector<double> spot_q50;
  std::vector<double> spot_q95;
  std::vector<double> gbm_q05;
  std::vector<double> gbm_q50;
  std::vector<double> gbm_q95;
  // Size: times.size() + 1.
  std::vector<int> slice_offsets;

  // Per futures pillar: the cost of carry it implies, ln(F / S) / T; the
  // futures' instantaneous vol today; and the Black-76 vol of an option on it
  // expiring at delivery.
  std::vector<double> implied_carry;
  std::vector<double> futures_vols;
  std::vector<double> option_vols;

  // Per node:
  std::vector<int> j;
  std::vector<double> x;
  std::vector<double> spots;
  // Probability of reaching the node from today.
  std::vector<double> probabilities;
  std::vector<double> pu;
  std::vector<double> pm;
  std::vector<double> pd;
  // Index of the middle successor (the others are one above and one below),
  // or -1 in the final slice.
  std::vector<int> successor_mid;
  // 0 for normal branching, 1 when it bends up (bottom edge), 2 when it bends
  // down (top edge).
  std::vector<int> branch_style;
};

absl::StatusOr<CommodityTreeResult> buildCommodityTree(
    const CommoditySpec& spec);

// An option on the futures delivering at `delivery_years`, expiring at
// `expiry_years` (no later). Both must fall on the tree's steps.
struct CommodityOptionSpec {
  double delivery_years = 0.25;
  double expiry_years = 0.25;
  double strike = 3.0;
  bool is_call = true;
  bool is_american = false;
};

struct CommodityOptionRequest {
  CommoditySpec commodity;
  CommodityOptionSpec option;
};

struct CommodityOptionResult {
  int delivery_index = 0;
  int expiry_index = 0;
  // Today's futures price for delivery.
  double futures_price = 0.0;
  // On the tree: the option as specified, and its European counterpart.
  double price = 0.0;
  double european_price = 0.0;
  // Black-76 with the Samuelson vol, which the European converges to.
  double black_price = 0.0;
  double implied_vol = 0.0;

  // Per node (NaN, or 0, after expiry): the futures price, the option's
  // value, and 1 where the holder exercises.
  std::vector<double> futures_values;
  std::vector<double> option_values;
  std::vector<int> exercised;
};

absl::StatusOr<CommodityOptionResult> priceCommodityOption(
    const CommodityOptionRequest& request);

}  // namespace smileexplorer::api

#endif  // SMILEEXPLORER_API_COMMODITY_API_H_
