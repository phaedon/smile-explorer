#include "derivatives/convexity_hedge.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numeric>
#include <vector>

#include "derivatives/sofr_futures.h"
#include "rates/zero_curve.h"

namespace smileexplorer {
namespace {

ZeroSpotCurve upwardCurve() {
  return ZeroSpotCurve({0.5, 1, 2, 5, 10}, {0.03, 0.032, 0.035, 0.04, 0.043});
}

struct Stats {
  double mean = 0.0;
  double sd = 0.0;
  // Standard error of the mean.
  double se = 0.0;
};

Stats stats(const std::vector<double>& v) {
  const double n = v.size();
  const double mean = std::accumulate(v.begin(), v.end(), 0.0) / n;
  double ss = 0.0;
  for (double x : v) ss += (x - mean) * (x - mean);
  const double sd = std::sqrt(ss / (n - 1));
  return {mean, sd, sd / std::sqrt(n)};
}

ConvexityTradeSpec spec() {
  ConvexityTradeSpec s;
  s.mean_reversion = 0.03;
  s.implied_sigma = 0.01;
  s.realized_sigma = 0.01;
  s.start = 3.0;
  s.end = 3.25;
  s.steps_per_year = 252;
  return s;
}

TEST(ConvexityHedgeTest, TodaysRatesMatchTheClosedForm) {
  const ZeroSpotCurve curve = upwardCurve();
  const ConvexityBatch b = simulateConvexityHedge(curve, spec(), 1, 7);
  EXPECT_NEAR(overnightForwardRate(curve, 3, 3.25), b.forward_rate, 1e-15);
  EXPECT_NEAR(
      sofrFuturesRate(curve, 0.03, 0.01, 3, 3.25), b.futures_rate, 1e-15);
  EXPECT_EQ(b.forward_rate, b.fixed_rate);
  EXPECT_NEAR(0.0, b.fra_value, 1e-9);
}

// With PAI, the FRA's margin account (with its interest, less the PAI) is its
// mark-to-market value, every day; at the end that is its payoff. So the
// margin changes when cash moves, not what the FRA is worth.
TEST(ConvexityHedgeTest, WithPaiTheCashIsTheMark) {
  const ZeroSpotCurve curve = upwardCurve();
  ConvexityTradeSpec s = spec();
  s.fixed_rate = 0.045;  // off market, so the FRA starts with a value
  const ConvexityBatch b = simulateConvexityHedge(curve, s, 20, 7, 0, 20);
  ASSERT_EQ(20u, b.recorded_paths);
  const size_t row = b.num_steps + 1;
  for (size_t p = 0; p < 20; ++p) {
    for (size_t k = 0; k < row; k += 17) {
      EXPECT_NEAR(b.fra_values[p * row + k], b.fra_cash[p * row + k], 1e-6);
    }
    // At the end, the value is the payoff: notional * tau * (K - realized).
    const double realized = b.futures_rates[p * row + row - 1];
    EXPECT_NEAR(
        1e6 * 0.25 * (0.045 - realized), b.fra_cash[p * row + row - 1], 1e-6);
  }
}

// Under the risk-neutral measure, the futures rate is a martingale: its
// average final settlement is today's futures rate. The FRA, collateralized
// with PAI, is worth zero at the forward rate. Per path, settlement - K +
// (FRA payoff, deflated) / (notional tau P(0, E)) averages to the difference
// between the two rates, with much less noise than either alone.
TEST(ConvexityHedgeTest, AverageSettlementIsTheFuturesRate) {
  const ZeroSpotCurve curve = upwardCurve();
  const int paths = 20000;
  ConvexityTradeSpec s = spec();
  s.steps_per_year = 52;
  const ConvexityBatch b = simulateConvexityHedge(curve, s, paths, 3, 0, paths);
  const size_t row = b.num_steps + 1;
  std::vector<double> settlements(paths);
  std::vector<double> differences(paths);
  for (int p = 0; p < paths; ++p) {
    settlements[p] = b.futures_rates[p * row + row - 1];
    differences[p] = settlements[p] - b.fixed_rate +
                     b.fra_pnls[p] / (1e6 * 0.25 * curve.df(3.25));
  }
  const Stats st = stats(settlements);
  EXPECT_NEAR(b.futures_rate, st.mean, 3 * st.se);
  const Stats diff = stats(differences);
  const double adjustment = b.futures_rate - b.forward_rate;
  EXPECT_GT(adjustment, 8 * diff.se);
  EXPECT_NEAR(adjustment, diff.mean, 3 * diff.se);
}

// Without PAI, the same margin would cost the FRA's receiver about the value
// of the convexity adjustment: with its margin settled, the FRA has become a
// future. (Only about: the FRA's margin follows its present value, discounted
// to the end, while a future's follows the undiscounted rate.)
TEST(ConvexityHedgeTest, WithoutPaiTheFraLosesTheAdjustment) {
  const ZeroSpotCurve curve = upwardCurve();
  const ConvexityBatch b = simulateConvexityHedge(curve, spec(), 20000, 11);
  // Per path, what the PAI was worth: the difference between the accounts.
  std::vector<double> lost(b.pnls.size());
  for (size_t p = 0; p < lost.size(); ++p) {
    lost[p] = b.no_pai_pnls[p] - b.fra_pnls[p];
  }
  const Stats st = stats(lost);
  const double adjustment_value =
      1e6 * 0.25 * (b.futures_rate - b.forward_rate) * curve.df(3.25);
  EXPECT_LT(st.mean, -20 * st.se);
  EXPECT_NEAR(
      -adjustment_value, st.mean, std::max(3 * st.se, 0.05 * adjustment_value));
}

// Hedged with tailed futures at the fair adjustment, the trade breaks even on
// average, and it is far less risky than the FRA alone.
TEST(ConvexityHedgeTest, TailedHedgeBreaksEven) {
  const ZeroSpotCurve curve = upwardCurve();
  const ConvexityBatch hedged = simulateConvexityHedge(curve, spec(), 4000, 5);
  const Stats st = stats(hedged.pnls);
  const double adjustment_value =
      1e6 * 0.25 * (hedged.futures_rate - hedged.forward_rate);
  EXPECT_NEAR(0.0, st.mean, std::max(3 * st.se, 0.03 * adjustment_value));

  ConvexityTradeSpec alone = spec();
  alone.hedge = FuturesHedgeStyle::kNone;
  const Stats fra = stats(simulateConvexityHedge(curve, alone, 4000, 5).pnls);
  EXPECT_LT(st.sd, fra.sd / 20);

  for (FuturesHedgeStyle style :
       {FuturesHedgeStyle::kUntailed, FuturesHedgeStyle::kStatic}) {
    ConvexityTradeSpec other = spec();
    other.hedge = style;
    const Stats o = stats(simulateConvexityHedge(curve, other, 4000, 5).pnls);
    EXPECT_GT(o.sd, st.sd);
  }
}

// The position is long convexity: it makes money when rates are more volatile
// than the futures' adjustment assumed, and loses when they are calmer.
TEST(ConvexityHedgeTest, ConvexityIsAVolatilityPosition) {
  const ZeroSpotCurve curve = upwardCurve();
  ConvexityTradeSpec s = spec();
  s.realized_sigma = 0.015;
  const Stats high = stats(simulateConvexityHedge(curve, s, 4000, 5).pnls);
  s.realized_sigma = 0.005;
  const Stats low = stats(simulateConvexityHedge(curve, s, 4000, 5).pnls);
  EXPECT_GT(high.mean, 5 * high.se);
  EXPECT_LT(low.mean, -5 * low.se);
}

TEST(ConvexityHedgeTest, StreamsAreReproducible) {
  const ZeroSpotCurve curve = upwardCurve();
  const ConvexityBatch b1 = simulateConvexityHedge(curve, spec(), 50, 9, 2);
  const ConvexityBatch b2 = simulateConvexityHedge(curve, spec(), 50, 9, 2);
  const ConvexityBatch b3 = simulateConvexityHedge(curve, spec(), 50, 9, 3);
  EXPECT_EQ(b1.pnls, b2.pnls);
  EXPECT_NE(b1.pnls, b3.pnls);
}

}  // namespace
}  // namespace smileexplorer
