#include "interest_rate_derivative.h"

#include <gtest/gtest.h>

#include <vector>

#include "derivative.h"
#include "instruments/swaps/fixed_cashflow_instrument.h"
#include "instruments/swaps/interest_rate_swap.h"
#include "rates/rates_curve.h"
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
    EXPECT_NEAR(
        .0694816,
        hullwhitecurve.forwardRate(0, 5.0, CompoundingPeriod::kContinuous),
        kOneBP * 0.1);

    FixedCashflowInstrument bond(&hullwhitecurve);
    ASSERT_TRUE(
        bond.setCashflows({Cashflow{.time_years = 9.0, .amount = 100.}}).ok());
    EXPECT_NEAR(100 * std::exp(-.073979 * 9), bond.price(), 0.01);

    auto swap = InterestRateSwap::createBond(std::move(bond));

    InterestRateDerivative bond_option(&hullwhitecurve, &swap);
    double bond_option_price =
        bond_option.price(VanillaOption(63., OptionPayoff::Put), 3.0);

    // TODO get this tolerance down to 0.0001
    EXPECT_NEAR(expected_tree_values[i], bond_option_price, 0.02);

    // Slightly bigger margin of error relative to Hull's calculation of the
    // analytic option value.
    EXPECT_NEAR(analytic_bond_option_value,
                bond_option_price,
                expected_max_tree_error * 2.0);  // TODO get this down to 1
  }
}

/**
 * @brief This test replicates the results in Hull, practice question 32.8 on
 * page 753. The solutions manual uses the analytical formula (32.10) but the
 * tree-based library reproduces the provided answer very closely.
 */
TEST(InterestRateDerivativeTest, CouponBondOption) {
  ZeroSpotCurve curve({0.25, 5},
                      {.06, .06},
                      CompoundingPeriod::kSemi,
                      CurveInterpolationStyle::kConstantForwards);

  ShortRateTreeCurve hullwhitecurve(
      std::make_unique<HullWhitePropagator>(.05, .015, 0.05), curve, 5.);

  FixedCashflowInstrument bond(&hullwhitecurve);
  ASSERT_TRUE(bond.setCashflows({Cashflow{.time_years = 3.0, .amount = 102.5},
                                 Cashflow{.time_years = 2.5, .amount = 2.5}})
                  .ok());
  bond.price();
  auto swap = InterestRateSwap::createBond(std::move(bond));
  InterestRateDerivative bond_option(&hullwhitecurve, &swap);
  double bond_option_price =
      bond_option.price(VanillaOption(99., OptionPayoff::Call), 2.1);
  EXPECT_NEAR(0.944596, bond_option_price, 0.0005);
}

}  // namespace smileexplorer
