#include "target_redemption_forward.h"

#include <gtest/gtest.h>

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
        tarf.price(125., 0.0002, 0.1, 10000, *foreign_curve_, *domestic_curve_)
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
        tarf.price(125., 0.0002, 0.1, 10000, *foreign_curve_, *domestic_curve_)
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
        tarf.price(125., 0.0002, 0.1, 10000, *foreign_curve_, *domestic_curve_)
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

  EXPECT_NEAR(135.657, strike, 0.002);
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

}  // namespace
}  // namespace smileexplorer
