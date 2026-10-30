#ifndef SMILEEXPLORER_API_SHORT_RATE_API_H_
#define SMILEEXPLORER_API_SHORT_RATE_API_H_

#include <vector>

#include "absl/status/statusor.h"
#include "api/pricing_api.h"

// The runtime API for short-rate trees: a Hull-White trinomial tree, fitted
// to a zero curve, with every node exposed so that the construction can be
// explained step by step.

namespace smileexplorer::api {

struct HullWhiteTreeSpec {
  // Speed of mean reversion, a (per year).
  double mean_reversion = 0.1;
  // Normal volatility of the short rate, sigma (as a decimal, per sqrt year).
  double sigma = 0.01;
  // Size of each (uniform) timestep, in years.
  double dt_years = 1.0;
  // The tree covers [0, horizon_years], rounded up to a whole number of steps.
  double horizon_years = 3.0;
  // The market curve that the tree is fitted to.
  RateCurveSpec curve;
  // Face value of a zero-coupon bond maturing at the end of the tree, priced
  // by backward induction.
  double bond_face = 100.0;
};

// The number of states per time slice grows by two each step until it
// reaches 2 * j_max + 1, so per-node data are ragged: the nodes of slice t
// are at indices slice_offsets[t] .. slice_offsets[t + 1] - 1, ordered from
// the lowest rate upwards.
struct HullWhiteTreeResult {
  // Spacing between neighbouring rates in a slice: sigma * sqrt(3 dt).
  double dr = 0.0;
  // Beyond j_max (in units of dr from the centre), branching bends inwards.
  int j_max = 0;
  // Price of the zero-coupon bond on the tree, and from the market curve.
  double bond_price = 0.0;
  double market_bond_price = 0.0;

  // Per time index:
  std::vector<double> times;
  // The shift that fits slice t to the market curve: short rate = x + alpha.
  std::vector<double> alphas;
  // Discount factor to times[t] from the market curve, and from the tree (the
  // sum of the slice's Arrow-Debreu prices).
  std::vector<double> market_dfs;
  std::vector<double> tree_dfs;
  // Size: times.size() + 1.
  std::vector<int> slice_offsets;

  // Per node:
  // Signed state index, from -j_max to j_max.
  std::vector<int> j;
  // The rate before fitting: j * dr.
  std::vector<double> x;
  std::vector<double> short_rates;
  // Probabilities of moving to the up, middle and down successors.
  std::vector<double> pu;
  std::vector<double> pm;
  std::vector<double> pd;
  // Index of the middle successor (the others are one above and one below),
  // or -1 in the final slice.
  std::vector<int> successor_mid;
  // 0 for normal branching, 1 when it bends up (bottom edge), 2 when it bends
  // down (top edge).
  std::vector<int> branch_style;
  std::vector<double> arrow_debreu;
  // Value of the zero-coupon bond at each node.
  std::vector<double> bond_values;
};

absl::StatusOr<HullWhiteTreeResult> buildHullWhiteTree(
    const HullWhiteTreeSpec& spec);

// An option on the zero-coupon bond that matures at the end of the tree.
struct BondOptionSpec {
  double strike = 100.0;
  // Snapped to the nearest step; it must fall strictly between today and the
  // bond's maturity.
  double expiry_years = 1.0;
  bool is_call = true;
  bool is_american = false;
};

struct BondOptionRequest {
  HullWhiteTreeSpec tree;
  BondOptionSpec option;
};

struct BondOptionResult {
  // The tree, including the bond's values.
  HullWhiteTreeResult tree;
  // The option's price on the tree, and its European counterpart's.
  double price = 0.0;
  double european_price = 0.0;
  // The closed-form European price (Hull-White, on the market curve).
  double analytic_price = 0.0;
  // Time index of the (snapped) expiry.
  int expiry_index = 0;
  // Per node, as in HullWhiteTreeResult (NaN after expiry):
  std::vector<double> option_values;
  // Payoff if exercised at the node.
  std::vector<double> exercise_values;
  // Discounted expected value of holding. NaN at expiry.
  std::vector<double> continuation_values;
};

absl::StatusOr<BondOptionResult> priceBondOption(
    const BondOptionRequest& request);

}  // namespace smileexplorer::api

#endif  // SMILEEXPLORER_API_SHORT_RATE_API_H_
