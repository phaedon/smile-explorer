#ifndef SMILEEXPLORER_API_CONVEXITY_API_H_
#define SMILEEXPLORER_API_CONVEXITY_API_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "api/pricing_api.h"
#include "api/short_rate_api.h"

// The runtime API for convexity adjustments between futures on a compounded
// overnight rate (such as CME's 3-month SOFR future) and collateralized
// forwards (FRAs) on the same period, in the Hull-White model fitted to the
// overnight curve: closed-form rates along the futures strip, the two
// rollbacks on the tree, and a Monte Carlo of the hedge between them.

namespace smileexplorer::api {

struct FuturesStripRequest {
  RateCurveSpec curve;
  double mean_reversion = 0.03;
  double sigma = 0.009;
  // The start of each contract's period, in years from today.
  std::vector<double> starts;
  double period_years = 0.25;
};

struct FuturesStripResult {
  // Per contract (each ending period_years after it starts):
  std::vector<double> starts;
  std::vector<double> ends;
  // The forward rate (an FRA's fair rate), and futures rates for a contract
  // compounding the overnight rate in arrears (SOFR) and for one on a term
  // rate fixed at the start (the old Eurodollar design), all simply
  // compounded, as decimals.
  std::vector<double> forward_rates;
  std::vector<double> futures_rates;
  std::vector<double> term_futures_rates;
  // Futures rate less forward rate.
  std::vector<double> adjustments;
  std::vector<double> term_adjustments;
};

absl::StatusOr<FuturesStripResult> futuresStrip(
    const FuturesStripRequest& request);

// One contract on the Hull-White tree. The tree's horizon_years and bond_face
// are ignored: the tree runs to the end of the period, which (like its start)
// must fall on the tree's steps.
struct FuturesTreeRequest {
  HullWhiteTreeSpec tree;
  double start_years = 1.0;
  double end_years = 1.25;
};

struct FuturesTreeResult {
  HullWhiteTreeResult tree;
  int start_index = 0;
  int end_index = 0;
  // At each node up to the start of the period (NaN after it): the futures
  // rate, from the undiscounted expectation of the compounding, and the
  // forward rate, from the ratio of bond prices (both rolled back on the
  // tree).
  std::vector<double> futures_rates;
  std::vector<double> forward_rates;
  // Today's, on the tree and in closed form.
  double futures_rate = 0.0;
  double forward_rate = 0.0;
  double analytic_futures_rate = 0.0;
  double analytic_forward_rate = 0.0;
};

absl::StatusOr<FuturesTreeResult> futuresOnTree(
    const FuturesTreeRequest& request);

// Receive fixed on a collateralized FRA, and sell SOFR futures against it.
struct ConvexityHedgeSpec {
  RateCurveSpec curve;
  double mean_reversion = 0.03;
  // Prices the futures and marks the FRA.
  double implied_sigma = 0.009;
  // Drives the paths.
  double realized_sigma = 0.009;
  double start_years = 2.0;
  double end_years = 2.25;
  double notional = 1e6;
  // The FRA's fixed rate is today's forward rate unless this is set, when it
  // is today's futures rate (as if no convexity adjustment had been made).
  bool fixed_at_futures_rate = false;
  // 0: tailed, rebalanced daily; 1: untailed; 2: static; 3: no hedge.
  int hedge_style = 0;
  int steps_per_year = 252;
};

struct ConvexityHedgeRequest {
  ConvexityHedgeSpec spec;
  int paths = 500;
  uint64_t seed = 1;
  // Which batch of the run this is (0 for the first).
  int batch_index = 0;
  // The number of paths, from the first, to return in full, and every how
  // many steps (the last step is always included).
  int recorded_paths = 0;
  int record_every = 1;
};

struct ConvexityHedgeResult {
  double forward_rate = 0.0;
  double futures_rate = 0.0;
  double fixed_rate = 0.0;
  double fra_value = 0.0;
  // The period, on the simulation's grid.
  double start_years = 0.0;
  double end_years = 0.0;
  // Today's discount factor to the end, and the value today of the futures'
  // convexity adjustment on the notional: notional * tau * adjustment * P.
  double end_discount_factor = 0.0;
  double adjustment_value = 0.0;

  // Per path, deflated by the path's bank account (so averages are values
  // today): the hedged position, the FRA (with PAI, its payoff), the futures,
  // and the FRA as if its margin carried no PAI.
  std::vector<double> pnls;
  std::vector<double> fra_pnls;
  std::vector<double> futures_pnls;
  std::vector<double> no_pai_pnls;
  // Sums over the batch, to combine batches: of pnls and their squares, and
  // of (no_pai - fra), the value of the PAI, and its squares.
  double pnl_sum = 0.0;
  double pnl_sum_squares = 0.0;
  double pai_value_sum = 0.0;
  double pai_value_sum_squares = 0.0;

  // The recorded paths, row by row, at each of `times`: as in
  // ConvexityBatch (rates as decimals, cash in currency, undiscounted).
  std::vector<double> times;
  int recorded_paths = 0;
  std::vector<double> short_rates;
  std::vector<double> futures_rates;
  std::vector<double> forward_rates;
  std::vector<double> hedge_ratios;
  std::vector<double> fra_values;
  std::vector<double> fra_cash;
  std::vector<double> no_pai_cash;
  std::vector<double> futures_cash;
};

absl::StatusOr<ConvexityHedgeResult> simulateConvexityHedgeBatch(
    const ConvexityHedgeRequest& request);

}  // namespace smileexplorer::api

#endif  // SMILEEXPLORER_API_CONVEXITY_API_H_
