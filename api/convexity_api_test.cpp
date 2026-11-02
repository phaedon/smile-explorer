#include "api/convexity_api.h"

#include <gtest/gtest.h>

#include <cmath>

#include "absl/status/status.h"

namespace smileexplorer::api {
namespace {

RateCurveSpec sofrCurve() {
  return {.maturities = {0.5, 1, 2, 5, 10},
          .rates = {0.042, 0.04, 0.037, 0.036, 0.038}};
}

TEST(ConvexityApiTest, StripGrowsAlongTheCurve) {
  FuturesStripRequest request{
      .curve = sofrCurve(), .mean_reversion = 0.03, .sigma = 0.009};
  for (int i = 0; i < 40; ++i) request.starts.push_back(0.25 * i);
  const auto strip = futuresStrip(request);
  ASSERT_TRUE(strip.ok()) << strip.status();
  ASSERT_EQ(40u, strip->adjustments.size());
  for (size_t i = 1; i < strip->adjustments.size(); ++i) {
    EXPECT_GT(strip->adjustments[i], strip->adjustments[i - 1]);
    // Compounding in arrears adds the period's own variance.
    EXPECT_GT(strip->adjustments[i], strip->term_adjustments[i]);
  }
  // A fraction of a basis point a year out; tens of them ten years out.
  EXPECT_LT(strip->adjustments[4], 1e-4);
  EXPECT_GT(strip->adjustments[39], 1e-3);
  EXPECT_NEAR(strip->futures_rates[8] - strip->forward_rates[8],
              strip->adjustments[8],
              1e-15);
}

TEST(ConvexityApiTest, StripRejectsBadInput) {
  FuturesStripRequest request{.curve = sofrCurve(), .starts = {1.0}};
  request.sigma = 0;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            futuresStrip(request).status().code());
  request.sigma = 0.01;
  request.starts = {};
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            futuresStrip(request).status().code());
  request.starts = {9.9};
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            futuresStrip(request).status().code());
}

TEST(ConvexityApiTest, TreeRollbacksMatchTheClosedForm) {
  const FuturesTreeRequest request{.tree = {.mean_reversion = 0.03,
                                            .sigma = 0.01,
                                            .dt_years = 1.0 / 24,
                                            .curve = sofrCurve()},
                                   .start_years = 3.0,
                                   .end_years = 3.25};
  const auto result = futuresOnTree(request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(72, result->start_index);
  EXPECT_EQ(78, result->end_index);
  EXPECT_NEAR(result->analytic_forward_rate, result->forward_rate, 1e-7);
  EXPECT_NEAR(result->analytic_futures_rate - result->analytic_forward_rate,
              result->futures_rate - result->forward_rate,
              1e-5);
  // Every node up to the start has both rates, futures above forward; after
  // the start, neither.
  const auto& offsets = result->tree.slice_offsets;
  for (int n = 0; n < offsets[result->start_index + 1]; ++n) {
    EXPECT_GT(result->futures_rates[n], result->forward_rates[n] - 1e-12);
  }
  EXPECT_TRUE(std::isnan(result->futures_rates[offsets.back() - 1]));
}

TEST(ConvexityApiTest, TreeRejectsOffGridPeriods) {
  const FuturesTreeRequest request{
      .tree = {.dt_years = 0.1, .curve = sofrCurve()},
      .start_years = 1.05,
      .end_years = 1.3};
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            futuresOnTree(request).status().code());
}

ConvexityHedgeRequest hedgeRequest() {
  return {.spec = {.curve = sofrCurve(),
                   .mean_reversion = 0.03,
                   .implied_sigma = 0.01,
                   .realized_sigma = 0.01,
                   .start_years = 2.0,
                   .end_years = 2.25},
          .paths = 2000,
          .seed = 4,
          .recorded_paths = 3,
          .record_every = 5};
}

TEST(ConvexityApiTest, HedgeBreaksEvenAndPaiIsWorthTheAdjustment) {
  const auto result = simulateConvexityHedgeBatch(hedgeRequest());
  ASSERT_TRUE(result.ok()) << result.status();
  const double n = result->pnls.size();
  const double mean = result->pnl_sum / n;
  const double se =
      std::sqrt((result->pnl_sum_squares / n - mean * mean) / (n - 1));
  EXPECT_GT(result->adjustment_value, 30);
  EXPECT_NEAR(0.0, mean, std::max(3 * se, 0.05 * result->adjustment_value));

  const double pai = result->pai_value_sum / n;
  const double pai_se =
      std::sqrt((result->pai_value_sum_squares / n - pai * pai) / (n - 1));
  EXPECT_NEAR(-result->adjustment_value,
              pai,
              std::max(3 * pai_se, 0.05 * result->adjustment_value));

  // 2.25 years of 252 steps is 567 steps; every 5th, and the last.
  ASSERT_EQ(115u, result->times.size());
  EXPECT_DOUBLE_EQ(2.25, result->times.back());
  EXPECT_EQ(3u * 115, result->fra_cash.size());
}

TEST(ConvexityApiTest, StrikingAtTheFuturesRateGivesAwayTheAdjustment) {
  ConvexityHedgeRequest request = hedgeRequest();
  request.spec.fixed_at_futures_rate = true;
  const auto result = simulateConvexityHedgeBatch(request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_DOUBLE_EQ(result->futures_rate, result->fixed_rate);
  // The receiver gains it; whoever paid at that rate gave it away.
  EXPECT_NEAR(result->adjustment_value, result->fra_value, 1e-6);
}

TEST(ConvexityApiTest, HedgeRejectsBadInput) {
  ConvexityHedgeRequest request = hedgeRequest();
  request.spec.hedge_style = 7;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            simulateConvexityHedgeBatch(request).status().code());
  request = hedgeRequest();
  request.recorded_paths = 5000;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            simulateConvexityHedgeBatch(request).status().code());
  request = hedgeRequest();
  request.spec.end_years = 2.001;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            simulateConvexityHedgeBatch(request).status().code());
}

}  // namespace
}  // namespace smileexplorer::api
