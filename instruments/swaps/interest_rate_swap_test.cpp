
#include "interest_rate_swap.h"

#include <gtest/gtest.h>

#include "contract.h"
#include "instruments/swaps/fixed_cashflow_instrument.h"
#include "instruments/swaps/floating_cashflow_instrument.h"
#include "rates/zero_curve.h"

namespace smileexplorer {
namespace {

class InterestRateSwapTest : public ::testing::Test {
 protected:
  InterestRateSwapTest() {
    curve = std::make_unique<ZeroSpotCurve>(
        std::vector<double>{1, 2, 5, 10},
        std::vector<double>{.03, .0325, .035, 0.04},
        CompoundingPeriod::kQuarterly,
        CurveInterpolationStyle::kConstantForwards);

    hullwhitecurve = std::make_unique<ShortRateTreeCurve>(
        std::make_unique<HullWhitePropagator>(0.1, 0.01, .25 / 17),
        *curve,
        12.);
  }

  std::unique_ptr<ZeroSpotCurve> curve;
  std::unique_ptr<ShortRateTreeCurve> hullwhitecurve;
};

TEST_F(InterestRateSwapTest, FloatingRateNotePricesAtPar) {
  const int maturity = 8.0;
  FloatingCashflowInstrument frn(hullwhitecurve.get());
  // We set "payer" so that the floating-rate leg is positive (inflows).
  frn.setCashflows(
      SwapContractDetails{.direction = SwapDirection::kPayer,
                          .floating_rate_frequency = ForwardRateTenor::k12Month,
                          .notional_principal = 100,
                          .start_date_years = 0,
                          .end_date_years = maturity});

  FixedCashflowInstrument principal(hullwhitecurve.get());
  principal.addCashflowToTree(Cashflow{.time_years = maturity, .amount = 100});

  InterestRateSwap swap(std::move(principal), std::move(frn));
  EXPECT_NEAR(100., swap.price(), 0.15);
}

TEST_F(InterestRateSwapTest, FixedRateBondPricesAtPar) {
  const int maturity_in_years = 8.0;

  double df_sum = 0.0;
  for (int i = 1; i <= maturity_in_years; ++i) {
    df_sum += hullwhitecurve->df(i);
  }
  double analytic_approx =
      (1.0 - hullwhitecurve->df(maturity_in_years)) / df_sum;

  FixedCashflowInstrument bond(hullwhitecurve.get());
  for (int i = 1; i <= maturity_in_years; ++i) {
    bond.addCashflowToTree(Cashflow{.time_years = static_cast<double>(i),
                                    .amount = analytic_approx * 100});
  }
  bond.addCashflowToTree(
      Cashflow{.time_years = maturity_in_years, .amount = 100});

  auto swap = InterestRateSwap::createBond(std::move(bond));
  EXPECT_NEAR(100., swap.price(), 0.15);
}

TEST_F(InterestRateSwapTest, OrdinaryFixedFloatingSwap) {
  constexpr int maturity_in_years = 5;

  //  Analytic approximation loop (semiannual pmts) matches the fixed-rate
  //  frequency below.
  double df_sum = 0.0;
  for (int i = 1; i <= maturity_in_years * 2; ++i) {
    df_sum += 0.5 * hullwhitecurve->df(i * 0.5);
  }
  double analytic_approx =
      (1.0 - hullwhitecurve->df(maturity_in_years)) / df_sum;

  SwapContractDetails contract{
      .direction = SwapDirection::kReceiver,
      .fixed_rate_frequency = CompoundingPeriod::kSemi,
      .floating_rate_frequency = ForwardRateTenor::k3Month,
      .notional_principal = 100.,
      .start_date_years = 0.,
      .end_date_years = maturity_in_years,
      .fixed_rate = analytic_approx};

  auto swap =
      InterestRateSwap::createFromContract(contract, hullwhitecurve.get());

  // TODO: Continue to get this error threshold down. $150k on a $100mm contract
  // over 5 years is just too high (on the order of ~5bps).
  EXPECT_NEAR(0.0, swap.price(), 0.150);
}

}  // namespace
}  // namespace smileexplorer
