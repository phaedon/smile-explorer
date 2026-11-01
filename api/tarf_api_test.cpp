#include "api/tarf_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "derivatives/target_redemption_forward.h"
#include "rates/zero_curve.h"

namespace smileexplorer::api {
namespace {

// EUR-USD with flat curves: 4% in dollars (domestic), 2% in euros.
TarfSpec eurUsd() {
  return {.spot = 1.1,
          .vol = 0.08,
          .domestic = {.maturities = {1, 5}, .rates = {0.04, 0.04}},
          .foreign = {.maturities = {1, 5}, .rates = {0.02, 0.02}},
          .notional = 1e6,
          .target = 5e4,
          .strike = 1.08,
          .leverage = 2,
          .end_years = 1,
          .fixing_frequency = 1 / 12.,
          .is_long = true,
          .steps_per_fixing = 4};
}

TEST(TarfApiTest, BatchMatchesTheLibrary) {
  const TarfBatchRequest request{.tarf = eurUsd(),
                                 .paths = 300,
                                 .seed = 5,
                                 .batch_index = 2,
                                 .recorded_paths = 10};
  const auto result = simulateTarfBatch(request);
  ASSERT_TRUE(result.ok()) << result.status();

  const ZeroSpotCurve domestic({1, 5}, {0.04, 0.04});
  const ZeroSpotCurve foreign({1, 5}, {0.02, 0.02});
  const TarfBatch batch =
      TargetRedemptionForward(
          TarfContractSpecs{.notional = 1e6,
                            .target = 5e4,
                            .strike = 1.08,
                            .end_date_years = 1,
                            .settlement_date_frequency = 1 / 12.,
                            .direction = FxTradeDirection::kLong,
                            .leverage = 2})
          .simulate(1.1, 0.08, 4, 300, foreign, domestic, 5, 2, 10);
  EXPECT_EQ(result->npvs, batch.npvs);
  EXPECT_EQ(result->knockout_fixings, batch.knockout_settlements);

  ASSERT_EQ(result->times.size(), 49u);
  EXPECT_DOUBLE_EQ(result->times.back(), 1.0);
  ASSERT_EQ(result->fixing_times.size(), 12u);
  for (size_t i = 0; i < 12; ++i) {
    const double t = result->fixing_times[i];
    EXPECT_NEAR(t, (i + 1) / 12., 1e-12);
    EXPECT_NEAR(result->forwards[i], 1.1 * std::exp(0.02 * t), 1e-9);
    EXPECT_NEAR(result->discount_factors[i], std::exp(-0.04 * t), 1e-9);
  }

  double sum = 0;
  double sum_squares = 0;
  for (double npv : result->npvs) {
    sum += npv;
    sum_squares += npv * npv;
  }
  EXPECT_NEAR(result->npv_sum, sum, 1e-6);
  EXPECT_NEAR(result->npv_sum_squares, sum_squares, 1e-6 * sum_squares);

  EXPECT_EQ(result->recorded_paths, 10);
  EXPECT_EQ(result->spots.size(), 10u * 49);
  EXPECT_EQ(result->payments.size(), 10u * 12);
  EXPECT_EQ(result->accumulated_profits.size(), 10u * 12);
}

TEST(TarfApiTest, EachBatchHasItsOwnPaths) {
  TarfBatchRequest request{.tarf = eurUsd(), .paths = 100};
  const auto first = simulateTarfBatch(request);
  request.batch_index = 1;
  const auto second = simulateTarfBatch(request);
  ASSERT_TRUE(first.ok() && second.ok());
  EXPECT_NE(first->npvs, second->npvs);
  // And asking again gives the same batch.
  EXPECT_EQ(second->npvs, simulateTarfBatch(request)->npvs);
}

TEST(TarfApiTest, ValuationPricesTheFirstBatchsPaths) {
  // A valuation steps from fixing to fixing on stream 0: the same NPVs as
  // the run's first batch with one step per fixing.
  TarfSpec tarf = eurUsd();
  tarf.steps_per_fixing = 1;
  const auto valuation = valueTarf({.tarf = tarf, .paths = 2000, .seed = 9});
  const auto batch =
      simulateTarfBatch({.tarf = tarf, .paths = 2000, .seed = 9});
  ASSERT_TRUE(valuation.ok() && batch.ok());
  EXPECT_NEAR(valuation->npv, batch->npv_sum / 2000, 1e-6);
  EXPECT_GT(valuation->standard_error, 0);
}

TEST(TarfApiTest, ZeroCostStrike) {
  const auto valuation = valueTarf({.tarf = eurUsd(), .paths = 4000});
  ASSERT_TRUE(valuation.ok()) << valuation.status();
  // The customer is short leveraged puts and holds calls capped by the
  // target, so the zero-cost strike is better (lower) than the forward.
  EXPECT_LT(valuation->zero_cost_strike, valuation->weighted_forward);
  EXPECT_NEAR(valuation->weighted_forward, 1.1 * std::exp(0.02 * 0.54), 2e-4);

  // At that strike, the same paths give an NPV of (nearly) zero: within the
  // bisection's tolerance on the strike, of 1 part in 10,000.
  TarfSpec at_zero_cost = eurUsd();
  at_zero_cost.strike = valuation->zero_cost_strike;
  const auto repriced = valueTarf({.tarf = at_zero_cost, .paths = 4000});
  ASSERT_TRUE(repriced.ok());
  EXPECT_LT(std::abs(repriced->npv),
            12 * 1e6 * 1e-4 * valuation->zero_cost_strike);
}

TEST(TarfApiTest, SweepsAreMonotone) {
  const TarfValuationRequest base{.tarf = eurUsd(), .paths = 2000};
  const auto check = [&](TarfSweepField field,
                         std::vector<double> values,
                         std::optional<bool> npv_decreasing,
                         bool strike_decreasing) {
    const auto sweep =
        sweepTarf({.valuation = base, .field = field, .values = values});
    ASSERT_TRUE(sweep.ok()) << sweep.status();
    ASSERT_EQ(sweep->size(), values.size());
    for (size_t i = 1; i < values.size(); ++i) {
      const auto& a = (*sweep)[i - 1];
      const auto& b = (*sweep)[i];
      if (npv_decreasing) EXPECT_EQ(b.npv < a.npv, *npv_decreasing) << i;
      EXPECT_EQ(b.zero_cost_strike < a.zero_cost_strike, strike_decreasing)
          << i;
    }
  };
  // Vega is negative, so the strike must come down to compensate.
  check(TarfSweepField::kVol, {0.04, 0.06, 0.08, 0.1, 0.12}, true, true);
  // A higher target lets the customer keep more of the gains, so the
  // zero-cost strike rises. At a fixed strike the NPV can go either way: the
  // contract also lives longer, with more chances of a leveraged loss.
  check(TarfSweepField::kTarget, {2e4, 5e4, 1e5, 2e5}, std::nullopt, false);
  // More leverage, larger losses.
  check(TarfSweepField::kLeverage, {1, 1.5, 2, 3}, true, true);
}

TEST(TarfApiTest, ValidationErrors) {
  const auto batchError = [](std::function<void(TarfBatchRequest&)> change) {
    TarfBatchRequest request{.tarf = eurUsd(), .paths = 10};
    change(request);
    return simulateTarfBatch(request).status();
  };
  const auto expectError = [](const absl::Status& status,
                              std::string_view text) {
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument) << text;
    EXPECT_THAT(status.message(), ::testing::HasSubstr(text));
  };

  expectError(batchError([](auto& r) { r.tarf.spot = 0; }), "spot");
  expectError(batchError([](auto& r) { r.tarf.vol = -0.1; }), "vol");
  expectError(batchError([](auto& r) { r.tarf.domestic.rates = {}; }),
              "domestic curve");
  expectError(batchError([](auto& r) { r.tarf.foreign.maturities = {2, 1}; }),
              "foreign curve");
  expectError(batchError([](auto& r) { r.tarf.notional = 0; }), "notional");
  expectError(batchError([](auto& r) { r.tarf.target = -1; }), "target");
  expectError(batchError([](auto& r) { r.tarf.strike = NAN; }), "strike");
  expectError(batchError([](auto& r) { r.tarf.leverage = 0.5; }), "leverage");
  expectError(batchError([](auto& r) { r.tarf.end_years = 0; }), "end_years");
  expectError(batchError([](auto& r) { r.tarf.fixing_frequency = 0; }),
              "fixing_frequency must be positive");
  expectError(batchError([](auto& r) { r.tarf.fixing_frequency = 0.3; }),
              "fixing_frequency must divide");
  expectError(batchError([](auto& r) { r.tarf.steps_per_fixing = 0; }),
              "steps_per_fixing");
  expectError(batchError([](auto& r) {
                r.tarf.end_years = 10;
                r.tarf.steps_per_fixing = 20;
              }),
              "steps in all");
  expectError(batchError([](auto& r) { r.paths = 0; }), "paths");
  expectError(batchError([](auto& r) { r.batch_index = -1; }), "batch_index");
  expectError(batchError([](auto& r) { r.recorded_paths = 11; }),
              "recorded_paths");

  expectError(valueTarf({.tarf = eurUsd(), .paths = 1}).status(), "paths");
  TarfSpec bad = eurUsd();
  bad.spot = -1;
  expectError(valueTarf({.tarf = bad}).status(), "spot");
  expectError(sweepTarf({.valuation = {.tarf = eurUsd()}}).status(), "values");
  expectError(sweepTarf({.valuation = {.tarf = eurUsd()},
                         .field = TarfSweepField::kLeverage,
                         .values = {2, 20}})
                  .status(),
              "leverage");
}

}  // namespace
}  // namespace smileexplorer::api
