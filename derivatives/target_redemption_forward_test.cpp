#include "target_redemption_forward.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numeric>

#include "derivatives/bsm.h"
#include "rates/zero_curve.h"

namespace smileexplorer {
namespace {

class TargetRedemptionForwardTest : public ::testing::Test {
 protected:
  void SetUp() override {
    foreign_curve_ =
        std::make_unique<ZeroSpotCurve>(std::vector<double>{1, 3},
                                        std::vector<double>{0.04, 0.04},
                                        CompoundingPeriod::kContinuous);
    domestic_curve_ =
        std::make_unique<ZeroSpotCurve>(std::vector<double>{1, 3},
                                        std::vector<double>{0.08, 0.08},
                                        CompoundingPeriod::kContinuous);
  }

  std::unique_ptr<ZeroSpotCurve> foreign_curve_;
  std::unique_ptr<ZeroSpotCurve> domestic_curve_;
};

TEST_F(TargetRedemptionForwardTest, DeterministicForwardWithoutTarget) {
  /*
  Verified in a spreadsheet:
   - expiry: 4 years
   - settlement frequency: quarterly (0.25)
   - underlying: USD-ISK
   - spot fx rate: 125.0
   - USD (foreign) rate: 4%
   - ISK (domestic) rate: 8%

  Rates are flat and continuously compounded.

  At this interest rate differential, the discount-weighted average of the
  forwards is 135.657.
  */

  EXPECT_NEAR(
      135.6570005,
      weightedAvgForward(125, 4.0, 0.25, *foreign_curve_, *domestic_curve_),
      1e-6);
}

TEST_F(TargetRedemptionForwardTest, AtmForwardHasZeroNPV) {
  /*
  This test uses the parameters described in DeterministicForwardWithoutTarget
  above.

  With a high-enough target (here, 100mm ISK) and a very low vol, this is just a
  strip of forwards. For reference, the accumulated profit of the profitable
  forwards would be 48mm ISK.
  */

  const double expected_npv = 0.;
  const double error_threshold = 20000;

  double atm_fwd_strike =
      weightedAvgForward(125, 4.0, 0.25, *foreign_curve_, *domestic_curve_);

  TargetRedemptionForward tarf(
      TarfContractSpecs{.notional = 1e6,
                        .target = 100e6,
                        .strike = atm_fwd_strike,
                        .end_date_years = 4.0,
                        .settlement_date_frequency = 0.25,
                        .direction = FxTradeDirection::kLong});

  for (int i = 0; i < 5; ++i) {
    double npv =
        tarf.price(
                125., 0.0002, 0.1, 10000, *foreign_curve_, *domestic_curve_, i)
            .mean_npv;
    EXPECT_LT(std::abs(npv - expected_npv), error_threshold);
  }
}

TEST_F(TargetRedemptionForwardTest, OtmForward) {
  /*
  Similar to the ATM example, but by setting the strike away from the wtd avg
  forward, we have a nonzero NPV. This is to ensure that the
  order-of-magnitude of the NPV computations is correct.

  At a strike of 131.9686 and the other params kept the same, this should
  result in an NPV of almost exactly 50mm ISK. This was verified / computed in
  a spreadsheet.
  */

  const double expected_npv = 50e6;
  const double error_threshold = 20000;

  TargetRedemptionForward tarf(
      TarfContractSpecs{.notional = 1e6,
                        .target = 100e6,
                        .strike = 131.9686,
                        .end_date_years = 4.0,
                        .settlement_date_frequency = 0.25,
                        .direction = FxTradeDirection::kLong});

  for (int i = 0; i < 5; ++i) {
    double npv =
        tarf.price(
                125., 0.0002, 0.1, 10000, *foreign_curve_, *domestic_curve_, i)
            .mean_npv;

    EXPECT_LT(std::abs(npv - expected_npv), error_threshold);
  }
}

TEST_F(TargetRedemptionForwardTest, KnockoutAlmostDeterministic) {
  /*
  Similar to the AtmForward case above, but with a 6mm ISK cumulative profit
  target. At a very low vol, this is almost certain to happen at year 2.75
  (with positive payments at t=[2.25, 2.5, 2.75]).

  The expected NPV is very negative here, because we are using the ATM fwd as
  the strike, yet we have a fairly low profit target and a large interest-rate
  differential. Therefore, there are large accumulated losses in the first 2
  years (around -39mm ISK, not discounted) and only +6mm ISK worth of profits
  due to the fixed target.
  */

  // This is the discounted amount of the approx. -33mm in total losses (-39mm
  // + 6mm):
  const double expected_npv = -31.75e6;
  const double error_threshold = 20000;

  double atm_fwd_strike =
      weightedAvgForward(125, 4.0, 0.25, *foreign_curve_, *domestic_curve_);

  TargetRedemptionForward tarf(
      TarfContractSpecs{.notional = 1e6,
                        .target = 6e6,
                        .strike = atm_fwd_strike,
                        .end_date_years = 4.0,
                        .settlement_date_frequency = 0.25,
                        .direction = FxTradeDirection::kLong});

  for (int i = 0; i < 5; ++i) {
    double npv =
        tarf.price(
                125., 0.0002, 0.1, 10000, *foreign_curve_, *domestic_curve_, i)
            .mean_npv;
    EXPECT_LT(std::abs(npv - expected_npv), error_threshold);
  }
}

TEST_F(TargetRedemptionForwardTest, VegaIsNegative) {
  TargetRedemptionForward tarf(
      TarfContractSpecs{.notional = 1e6,
                        .target = 6e6,
                        .strike = 131,
                        .end_date_years = 4.0,
                        .settlement_date_frequency = 0.25,
                        .direction = FxTradeDirection::kLong});

  double vol_low = 0.05;
  double npv_vol_lower =
      tarf.price(125., vol_low, 0.1, 10000, *foreign_curve_, *domestic_curve_)
          .mean_npv;
  double npv_vol_higher = tarf.price(125.,
                                     vol_low + 0.01,
                                     0.1,
                                     10000,
                                     *foreign_curve_,
                                     *domestic_curve_)
                              .mean_npv;

  EXPECT_GT(npv_vol_lower, npv_vol_higher);
}

TEST_F(TargetRedemptionForwardTest, FindZeroNPVStrike) {
  TarfContractSpecs specs{.notional = 1e6,
                          .target = 100e6,
                          .strike = 125,
                          .end_date_years = 4.0,
                          .settlement_date_frequency = 0.25,
                          .direction = FxTradeDirection::kLong};

  const double strike =
      findZeroNPVStrike(specs, 125, 0.0001, *foreign_curve_, *domestic_curve_);

  EXPECT_NEAR(135.657, strike, 0.005);
}

TEST_F(TargetRedemptionForwardTest, LoweringTargetReducesLongStrike) {
  TarfContractSpecs specs{.notional = 1e6,
                          .target = 6e6,
                          .strike = 125,
                          .end_date_years = 4.0,
                          .settlement_date_frequency = 0.25,
                          .direction = FxTradeDirection::kLong};
  const double strike_6mm =
      findZeroNPVStrike(specs, 125, 0.05, *foreign_curve_, *domestic_curve_);

  specs.target = 5e6;
  const double strike_5mm =
      findZeroNPVStrike(specs, 125, 0.05, *foreign_curve_, *domestic_curve_);

  EXPECT_LT(strike_5mm, strike_6mm);
}

TEST_F(TargetRedemptionForwardTest, ShortFxPositionHasHigherStrikeThanLong) {
  TarfContractSpecs specs{.notional = 1e6,
                          .target = 6e6,
                          .strike = 125,
                          .end_date_years = 4.0,
                          .settlement_date_frequency = 0.25,
                          .direction = FxTradeDirection::kLong};
  const double strike_long =
      findZeroNPVStrike(specs, 125, 0.05, *foreign_curve_, *domestic_curve_);

  specs.direction = FxTradeDirection::kShort;
  const double strike_short =
      findZeroNPVStrike(specs, 125, 0.05, *foreign_curve_, *domestic_curve_);

  EXPECT_GT(strike_short, strike_long);
}

// The mean and standard error of a sample.
std::pair<double, double> meanAndStandardError(const std::vector<double>& xs) {
  const double n = xs.size();
  const double mean = std::accumulate(xs.begin(), xs.end(), 0.0) / n;
  double sum_sq = 0.0;
  for (double x : xs) sum_sq += (x - mean) * (x - mean);
  return {mean, std::sqrt(sum_sq / (n - 1) / n)};
}

TEST_F(TargetRedemptionForwardTest, WithoutTargetIsAStripOfOptions) {
  /*
  With a target that can't be reached, each settlement pays the customer
  N * (S - K) when it gains and leverage * N * (S - K) when it loses: a long
  call and `leverage` short puts, struck at K. Their Garman-Kohlhagen prices
  (Black-Scholes with the foreign rate as the dividend yield) give the NPV.
  */
  const double strike = 130;
  const double vol = 0.1;
  const double leverage = 2;
  for (const auto direction :
       {FxTradeDirection::kLong, FxTradeDirection::kShort}) {
    TargetRedemptionForward tarf(
        TarfContractSpecs{.notional = 1e6,
                          .target = 1e15,
                          .strike = strike,
                          .end_date_years = 4.0,
                          .settlement_date_frequency = 0.25,
                          .direction = direction,
                          .leverage = leverage});

    double expected_npv = 0.0;
    // The value of one call and one put, for scale.
    double gross = 0.0;
    for (int i = 1; i <= 16; ++i) {
      const double t = 0.25 * i;
      const double call_value = call(125, strike, vol, t, 0.08, 0.04);
      const double put_value = put(125, strike, vol, t, 0.08, 0.04);
      expected_npv += direction == FxTradeDirection::kLong
                          ? 1e6 * (call_value - leverage * put_value)
                          : 1e6 * (put_value - leverage * call_value);
      gross += 1e6 * (call_value + put_value);
    }

    const auto result = tarf.price(
        125., vol, 0.25, 50000, *foreign_curve_, *domestic_curve_, 3);
    const auto [mean, standard_error] = meanAndStandardError(result.path_npvs);
    EXPECT_NEAR(mean, result.mean_npv, 1e-6 * std::abs(mean));
    EXPECT_NEAR(expected_npv, mean, 3 * standard_error);
    // And the standard error is small enough for the test to mean something.
    EXPECT_LT(standard_error, 0.01 * gross) << gross;
  }
}

TEST_F(TargetRedemptionForwardTest, BatchesAreReproducible) {
  TargetRedemptionForward tarf(
      TarfContractSpecs{.notional = 1e6,
                        .target = 6e6,
                        .strike = 131,
                        .end_date_years = 4.0,
                        .settlement_date_frequency = 0.25,
                        .direction = FxTradeDirection::kLong,
                        .leverage = 2});
  const auto batch = [&](uint64_t seed, uint64_t stream) {
    return tarf.simulate(
        125., 0.1, 4, 200, *foreign_curve_, *domestic_curve_, seed, stream, 5);
  };
  const TarfBatch a = batch(7, 3);
  EXPECT_EQ(a.npvs, batch(7, 3).npvs);
  EXPECT_EQ(a.knockout_settlements, batch(7, 3).knockout_settlements);
  EXPECT_NE(a.npvs, batch(7, 4).npvs);
  EXPECT_NE(a.npvs, batch(8, 3).npvs);
}

TEST_F(TargetRedemptionForwardTest, RecordedPathsMatchTheirNpvs) {
  const double leverage = 2;
  TargetRedemptionForward tarf(
      TarfContractSpecs{.notional = 1e6,
                        .target = 6e6,
                        .strike = 131,
                        .end_date_years = 4.0,
                        .settlement_date_frequency = 0.25,
                        .direction = FxTradeDirection::kLong,
                        .leverage = leverage});
  const int m = 3;
  const TarfBatch batch = tarf.simulate(
      125., 0.1, m, 200, *foreign_curve_, *domestic_curve_, 11, 0, 50);
  ASSERT_EQ(batch.num_settlements, 16);
  ASSERT_EQ(batch.steps_per_settlement, m);
  EXPECT_DOUBLE_EQ(batch.dt, 0.25 / m);
  ASSERT_EQ(batch.recorded_paths, 50u);
  ASSERT_EQ(batch.spots.size(), 50u * (16 * m + 1));

  int knocked_out = 0;
  for (size_t p = 0; p < batch.recorded_paths; ++p) {
    const double* spots = &batch.spots[p * (16 * m + 1)];
    const double* payments = &batch.payments[p * 16];
    const double* profits = &batch.accumulated_profits[p * 16];
    const int ko = batch.knockout_settlements[p];
    const int last = ko < 0 ? 15 : ko;
    knocked_out += ko >= 0;
    EXPECT_EQ(spots[0], 125.);

    double npv = 0.0;
    double profit = 0.0;
    for (int i = 0; i <= last; ++i) {
      const double gain = spots[(i + 1) * m] - 131;
      const double payment = 1e6 * gain * (gain < 0 ? leverage : 1.0);
      if (i < ko) {
        EXPECT_NEAR(payment, payments[i], 1e-6);
      } else if (i == ko) {
        // The knockout payment is cut to land exactly on the target.
        EXPECT_LT(payments[i], payment);
      }
      if (payments[i] > 0) profit += payments[i];
      EXPECT_NEAR(profit, profits[i], 1e-6);
      npv += payments[i] * domestic_curve_->df(0.25 * (i + 1));
    }
    EXPECT_NEAR(npv, batch.npvs[p], 1e-6);
    if (ko >= 0) {
      EXPECT_NEAR(profits[ko], 6e6, 1e-6);
      EXPECT_TRUE(std::isnan(spots[(ko + 1) * m + 1]) || ko == 15);
      if (ko < 15) EXPECT_TRUE(std::isnan(payments[ko + 1]));
    } else {
      EXPECT_LE(profits[15], 6e6);
    }
  }
  // Both kinds of path are exercised.
  EXPECT_GT(knocked_out, 0);
  EXPECT_LT(knocked_out, 50);
}

TEST_F(TargetRedemptionForwardTest, KnockoutSettlements) {
  TarfContractSpecs specs{
      .notional = 1e6,
      .target = 6e6,
      .strike =
          weightedAvgForward(125, 4.0, 0.25, *foreign_curve_, *domestic_curve_),
      .end_date_years = 4.0,
      .settlement_date_frequency = 0.25,
      .direction = FxTradeDirection::kLong};
  // At a very low vol, the target is reached at year 2.75, the 11th
  // settlement (see KnockoutAlmostDeterministic).
  const TarfBatch batch = TargetRedemptionForward(specs).simulate(
      125., 0.0002, 2, 500, *foreign_curve_, *domestic_curve_, 1);
  for (int ko : batch.knockout_settlements) EXPECT_EQ(ko, 10);

  specs.target = 1e15;
  const TarfBatch never = TargetRedemptionForward(specs).simulate(
      125., 0.2, 2, 500, *foreign_curve_, *domestic_curve_, 1);
  for (int ko : never.knockout_settlements) EXPECT_EQ(ko, -1);
}

TEST_F(TargetRedemptionForwardTest, PriceRoundsTheTimestep) {
  TargetRedemptionForward tarf(
      TarfContractSpecs{.notional = 1e6,
                        .target = 6e6,
                        .strike = 131,
                        .end_date_years = 4.0,
                        .settlement_date_frequency = 0.25,
                        .direction = FxTradeDirection::kLong});
  const auto npv = [&](double dt) {
    return tarf.price(125., 0.1, dt, 500, *foreign_curve_, *domestic_curve_)
        .mean_npv;
  };
  // A timestep longer than the settlement period is capped at one step per
  // period, and one that doesn't divide it is rounded: both use the same
  // random numbers as the step they round to.
  EXPECT_EQ(npv(1.0), npv(0.25));
  EXPECT_EQ(npv(0.13), npv(0.125));
  EXPECT_EQ(
      tarf.price(125., 0.1, 0.1, 0, *foreign_curve_, *domestic_curve_).mean_npv,
      0.0);
}

TEST_F(TargetRedemptionForwardTest, LeverageLowersLongStrike) {
  TarfContractSpecs specs{.notional = 1e6,
                          .target = 6e6,
                          .strike = 125,
                          .end_date_years = 4.0,
                          .settlement_date_frequency = 0.25,
                          .direction = FxTradeDirection::kLong};
  const double strike_unlevered =
      findZeroNPVStrike(specs, 125, 0.05, *foreign_curve_, *domestic_curve_);

  specs.leverage = 2;
  const double strike_levered =
      findZeroNPVStrike(specs, 125, 0.05, *foreign_curve_, *domestic_curve_);

  EXPECT_LT(strike_levered, strike_unlevered);
}

}  // namespace
}  // namespace smileexplorer
