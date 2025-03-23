#include "interest_rate_derivative.h"

#include <gtest/gtest.h>

#include <vector>

#include "derivative.h"
#include "rates/fixed_cashflow_instrument.h"
#include "rates/rates_curve.h"
#include "trees/propagators.h"
#include "trees/stochastic_tree_model.h"
#include "vanilla_option.h"

namespace smileexplorer {

// This test replicates the results in Example 32.1 in Hull, "Options,
// Futures..." (11th ed., pg 748).
// =============================
// NOTE: The tolerances are somewhat higher than expected given the precision
// specified in Hull. However, he mentions that the zero coupon rates should be
// linearly interpolated. Since we are using constant forwards, this is not
// exactly equivalent. Adding a lin interp flag to the ZeroSpotCurve class
// should get much closer to the expected values. If not, then there is another
// bug to figure out.
TEST(InterestRateDerivativeTest, EuropeanBondOption) {
  std::vector<double> maturities{3,
                                 31,
                                 62,
                                 94,
                                 185,
                                 367,
                                 731,
                                 1096,
                                 1461,
                                 1826,
                                 2194,
                                 2558,
                                 2922,
                                 3287,
                                 3653};
  for (auto& m : maturities) {
    m /= 365.;
  }

  std::vector<double> rates = {.0501772,
                               .0498284,
                               .0497234,
                               .0496157,
                               .0499058,
                               .0509389,
                               .0579733,
                               .0630595,
                               .0673464,
                               .0694816,
                               .0708807,
                               .0727527,
                               .0730852,
                               .073979,
                               .0749015};
  ZeroSpotCurve curve(maturities,
                      rates,
                      CompoundingPeriod::kContinuous,
                      CurveInterpolationStyle::kMonotonePiecewiseCubicZeros);

  // Sanity check to ensure that the input rates were entered correctly.
  constexpr double kOneBP = 0.0001;
  EXPECT_NEAR(.0694816,
              curve.forwardRate(0.0, 5.0, CompoundingPeriod::kContinuous),
              kOneBP * 0.05);

  const double mean_reversion_speed = 0.1;
  const double sigma = 0.01;

  // This table is directly from the example in Hull.
  std::vector<int> steps{10, 30, 50, 100, 200, 500};
  std::vector<double> expected_tree_values{
      1.8468, 1.8172, 1.8057, 1.8128, 1.8090, 1.8091};
  const double analytic_bond_option_value = 1.8093;
  const double expected_max_tree_error =
      std::abs(expected_tree_values[0] - analytic_bond_option_value);

  for (size_t i = 0; i < steps.size(); ++i) {
    double dt = 3.0 / steps[i];
    ShortRateTreeCurve hullwhitecurve(
        std::make_unique<HullWhitePropagator>(mean_reversion_speed, sigma, dt),
        curve,
        11.);

    // Sanity check to ensure that the short-rate tree fits the market
    // zeros correctly.
    // TODO: Get this down to a tenth of a bp.
    EXPECT_NEAR(
        .0694816,
        hullwhitecurve.forwardRate(0, 5.0, CompoundingPeriod::kContinuous),
        kOneBP * 0.7);

    FixedCashflowInstrument bond(&hullwhitecurve);
    bond.setCashflows({Cashflow{.time_years = 9.0, .amount = 100.}});
    EXPECT_NEAR(100 * std::exp(-.073979 * 9), bond.price(), 0.10);

    InterestRateDerivative bond_option(&hullwhitecurve, &bond);
    double bond_option_price =
        bond_option.price(VanillaOption(63., OptionPayoff::Put), 3.0);

    // TODO get this tolerance down to 0.0001
    EXPECT_NEAR(expected_tree_values[i], bond_option_price, 0.05);

    // Slightly bigger margin of error relative to Hull's calculation of the
    // analytic option value.
    EXPECT_NEAR(analytic_bond_option_value,
                bond_option_price,
                expected_max_tree_error * 2.4);  // TODO get this down to 1
  }
}

struct SwapContractDetails {
  SwapDirection direction;
  CompoundingPeriod fixed_rate_frequency;
  ForwardRateTenor floating_rate_frequency;
  double notional_principal;
  double start_date_years;
  double end_date_years;
  double fixed_rate;
};

class InterestRateSwap {
 public:
  InterestRateSwap(SwapContractDetails contract,
                   const ShortRateTreeCurve* curve)
      : contract_(contract),
        curve_(curve),
        fixed_leg_(curve),
        floating_leg_(curve) {
    double fixed_rate_tenor =
        1. / static_cast<int>(contract.fixed_rate_frequency);
    for (double t = contract.start_date_years + fixed_rate_tenor;
         t <= contract.end_date_years;
         t += fixed_rate_tenor) {
      Cashflow cashflow{
          .time_years = t,
          .amount = contract.fixed_rate * contract.notional_principal *
                    fixed_rate_tenor * static_cast<int>(contract.direction)};
      fixed_leg_.addCashflowToTree(cashflow);
    }

    floating_leg_.setCashflows(contract.notional_principal,
                               contract.floating_rate_frequency,
                               contract.direction,
                               contract.start_date_years,
                               contract.end_date_years);
  }

  double price() { return fixed_leg_.price() + floating_leg_.price(); }

 private:
  SwapContractDetails contract_;
  const ShortRateTreeCurve* curve_;
  FixedCashflowInstrument fixed_leg_;
  FloatingCashflowInstrument floating_leg_;
};

TEST(InterestRateDerivativeTest, Swap_APIUnderDevelopment) {
  ZeroSpotCurve curve({1, 2, 5, 10},
                      {.03, .0325, .035, 0.04},
                      CompoundingPeriod::kAnnual,
                      CurveInterpolationStyle::kMonotonePiecewiseCubicZeros);

  ShortRateTreeCurve hullwhitecurve(
      std::make_unique<HullWhitePropagator>(0.1, 0.01, .025), curve, 12.);

  //  Analytic approximation loop (20 semiannual pmts) matches the fixed-rate
  //  frequency below.
  double df_sum = 0.0;
  for (int i = 1; i <= 20; ++i) {
    df_sum += 0.5 * curve.df(i * 0.5);
  }
  double analytic_approx = (1.0 - curve.df(10.)) / df_sum;
  std::cout << "Swap rate analytic approx ==== " << analytic_approx
            << std::endl;

  SwapContractDetails contract{
      .direction = SwapDirection::kReceiver,
      .fixed_rate_frequency = CompoundingPeriod::kSemi,
      .floating_rate_frequency = ForwardRateTenor::k3Month,
      .notional_principal = 100.,
      .start_date_years = 0.,
      .end_date_years = 10.,
      .fixed_rate = analytic_approx};

  InterestRateSwap swap(contract, &hullwhitecurve);

  EXPECT_NEAR(0.0, swap.price(), 0.001);
}

}  // namespace smileexplorer
