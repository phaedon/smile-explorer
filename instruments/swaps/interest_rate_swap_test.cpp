
#include "interest_rate_swap.h"

#include <gtest/gtest.h>

#include "contract.h"
#include "fixed_cashflow_instrument.h"
#include "rates/zero_curve.h"

namespace smileexplorer {
namespace {

TEST(InterestRateSwapTest, FloatingRateNotePricesAtPar) {
  ZeroSpotCurve curve({1, 2, 5, 10},
                      {.03, .0325, .035, 0.04},
                      CompoundingPeriod::kAnnual,
                      CurveInterpolationStyle::kConstantForwards);

  ShortRateTreeCurve hullwhitecurve(
      std::make_unique<HullWhitePropagator>(0.1, 0.001, .5), curve, 12.);

  const int maturity = 2.0;
  FloatingCashflowInstrument frn(&hullwhitecurve);
  // We set "payer" so that the floating-rate leg is positive (inflows).
  frn.setCashflows(
      100, ForwardRateTenor::k12Month, SwapDirection::kPayer, 0, maturity);

  FixedCashflowInstrument principal(&hullwhitecurve);
  principal.addCashflowToTree(Cashflow{.time_years = maturity, .amount = 100});

  InterestRateSwap swap(std::move(principal), std::move(frn));
  EXPECT_NEAR(100., swap.price(), 0.001);
}

/*
TEST(InterestRateSwapTest, Swap_APIUnderDevelopment) {
  ZeroSpotCurve curve({1, 2, 5, 10},
                      {.03, .0325, .035, 0.04},
                      CompoundingPeriod::kAnnual,
                      CurveInterpolationStyle::kMonotonePiecewiseCubicZeros);

  ShortRateTreeCurve hullwhitecurve(
      std::make_unique<HullWhitePropagator>(0.1, 0.001, .25), curve, 12.);

  constexpr int maturity_in_years = 2;

  //  Analytic approximation loop (20 semiannual pmts) matches the fixed-rate
  //  frequency below.
  double df_sum = 0.0;
  for (int i = 1; i <= maturity_in_years * 2; ++i) {
    df_sum += 0.5 * curve.df(i * 0.5);
  }
  double analytic_approx = (1.0 - curve.df(maturity_in_years)) / df_sum;
  std::cout << "Swap rate analytic approx ==== " << analytic_approx
            << std::endl;

  SwapContractDetails contract{
      .direction = SwapDirection::kReceiver,
      .fixed_rate_frequency = CompoundingPeriod::kSemi,
      .floating_rate_frequency = ForwardRateTenor::k3Month,
      .notional_principal = 100.,
      .start_date_years = 0.,
      .end_date_years = maturity_in_years,
      .fixed_rate = analytic_approx};

  auto swap = InterestRateSwap::createFromContract(contract, &hullwhitecurve);

  EXPECT_NEAR(0.0, swap.price(), 0.01);
}
  */

}  // namespace
}  // namespace smileexplorer
