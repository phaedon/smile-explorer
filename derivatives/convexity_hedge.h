#ifndef SMILEEXPLORER_DERIVATIVES_CONVEXITY_HEDGE_H_
#define SMILEEXPLORER_DERIVATIVES_CONVEXITY_HEDGE_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "rates/rates_curve.h"

// A Monte Carlo of the trade that captures the convexity adjustment: receive
// fixed on a collateralized FRA on the compounded overnight rate over [start,
// end], and sell futures on the same period, rebalancing daily.
//
// Both contracts are margined daily, but differently:
//  - The FRA's variation margin is collateral: whoever holds it pays interest
//    on it at the overnight rate (price alignment interest, PAI). So the cash
//    the FRA has delivered, with interest, always equals its mark-to-market
//    value, and at the end its payoff: margined or not, it pays at `end`.
//  - The future's variation margin is settled: no interest is paid on it. The
//    gains are reinvested, and losses funded, at overnight rates that move
//    with them.
//
// Paths of the short rate come from the Hull-White model with
// `realized_sigma`; both contracts are marked, and the hedge computed, with
// `implied_sigma`. When the two agree, the hedge earns back on average exactly
// what the futures' convexity adjustment cost; when rates are more volatile
// than implied, the position (which is long convexity) makes money.

namespace smileexplorer {

enum class FuturesHedgeStyle {
  // Sell the futures that offset the FRA's sensitivity to the rate, daily.
  // Futures pay their margin now and the FRA pays at the end, so this is
  // fewer futures than FRAs: the hedge is "tailed" by the discount factor.
  kTailed,
  // The same, without the tail: as if futures margin, too, were paid at end.
  kUntailed,
  // The tailed hedge on the first day, never rebalanced.
  kStatic,
  // The FRA alone.
  kNone,
};

struct ConvexityTradeSpec {
  double mean_reversion = 0.03;
  double implied_sigma = 0.009;
  double realized_sigma = 0.009;
  // The period, in years; both are rounded to whole timesteps.
  double start = 2.0;
  double end = 2.25;
  // Per FRA, and per unit of futures (one CME 3-month SOFR contract is $1mm
  // on a quarter: $25 per basis point).
  double notional = 1e6;
  // The FRA's fixed rate; NaN means today's forward rate (an at-market FRA,
  // worth zero).
  double fixed_rate = std::numeric_limits<double>::quiet_NaN();
  FuturesHedgeStyle hedge = FuturesHedgeStyle::kTailed;
  int steps_per_year = 252;
};

struct ConvexityBatch {
  int num_steps = 0;
  double dt = 0.0;
  // Time index at which the period starts (the period ends at num_steps).
  int start_index = 0;
  // Today's rates (with implied_sigma), and the FRA's fixed rate.
  double forward_rate = 0.0;
  double futures_rate = 0.0;
  double fixed_rate = 0.0;
  // Today's value of the FRA.
  double fra_value = 0.0;

  // Per path, the cash at the end divided by the path's bank account (so
  // their averages are values today): the FRA with its margin and PAI, which
  // is its payoff; the FRA had no PAI been paid on its margin; the futures;
  // and the hedged position (FRA with PAI, plus futures).
  std::vector<double> fra_pnls;
  std::vector<double> no_pai_pnls;
  std::vector<double> futures_pnls;
  std::vector<double> pnls;

  // The recorded paths, row by row, at each time index 0..num_steps: the short
  // rate, the futures rate and the FRA's fair rate (both as marked), the
  // futures sold per FRA (held over the step that follows), the FRA's value,
  // and the cash accounts (with interest, undiscounted) of the FRA with PAI,
  // without it, and the futures.
  size_t recorded_paths = 0;
  std::vector<double> short_rates;
  std::vector<double> futures_rates;
  std::vector<double> forward_rates;
  std::vector<double> hedge_ratios;
  std::vector<double> fra_values;
  std::vector<double> fra_cash;
  std::vector<double> no_pai_cash;
  std::vector<double> futures_cash;
};

// The random numbers come only from `seed` and `stream`, so a run split into
// batches (one stream per batch) is reproducible.
ConvexityBatch simulateConvexityHedge(const RatesCurve& curve,
                                      const ConvexityTradeSpec& spec,
                                      size_t num_paths,
                                      uint64_t seed,
                                      uint64_t stream = 0,
                                      size_t recorded_paths = 0);

// The generator for one stream of a seeded run.
std::mt19937_64 convexityRng(uint64_t seed, uint64_t stream);

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_DERIVATIVES_CONVEXITY_HEDGE_H_
