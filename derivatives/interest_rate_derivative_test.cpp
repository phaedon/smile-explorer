#include "interest_rate_derivative.h"

#include <gtest/gtest.h>

#include <algorithm>
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

// A put on a zero-coupon bond is worth exercising early when rates are high
// (and the bond cheap), so the American put is worth more than the European
// one; at no node is it worth less than its exercise value.
TEST(InterestRateDerivativeTest, AmericanBondOption) {
  ZeroSpotCurve curve({1, 5, 10}, {0.05, 0.06, 0.065});
  const double dt = 0.25;
  ShortRateTreeCurve hullwhitecurve(
      std::make_unique<HullWhitePropagator>(0.1, 0.01, dt), curve, 9.);
  FixedCashflowInstrument bond(&hullwhitecurve);
  ASSERT_TRUE(
      bond.setCashflows({Cashflow{.time_years = 9.0, .amount = 100.}}).ok());
  bond.price();
  auto swap = InterestRateSwap::createBond(std::move(bond));

  InterestRateDerivative european(&hullwhitecurve, &swap);
  const double european_price =
      european.price(VanillaOption(63., OptionPayoff::Put), 3.0);
  InterestRateDerivative american(&hullwhitecurve, &swap);
  const double american_price = american.price(
      VanillaOption(63., OptionPayoff::Put, ExerciseStyle::American), 3.0);

  EXPECT_GT(american_price, european_price + 0.01);
  const auto& bond_tree = swap.trinomialTree();
  for (int ti = 0; ti <= 12; ++ti) {
    for (int i = 0; i < american.tree().numStatesAt(ti); ++i) {
      EXPECT_GE(american.tree().nodeValue(ti, i),
                std::max(0.0, 63. - bond_tree.nodeValue(ti, i)) - 1e-12);
    }
  }
}

// A payer Bermudan swaption: exercisable at 2, 3, 4, 5 and 6 years into the
// remainder of a swap paying 4% annually to 7 years. Each exercise is a put,
// struck at par, on the ex-coupon bond of the swap's remaining fixed leg.
class BermudanSwaptionTest : public ::testing::Test {
 protected:
  BermudanSwaptionTest()
      : curve_({1, 3, 5, 10}, {0.035, 0.037, 0.04, 0.042}),
        tree_(std::make_unique<HullWhitePropagator>(0.1, 0.01, 0.25),
              curve_,
              7.) {}

  // The fixed leg from `start` onwards (coupons after it, and the notional).
  InterestRateSwap bondFrom(double start) {
    FixedCashflowInstrument bond(&tree_);
    std::vector<Cashflow> cashflows;
    for (int year = static_cast<int>(start) + 1; year <= 7; ++year) {
      cashflows.push_back({.time_years = static_cast<double>(year),
                           .amount = year == 7 ? 104. : 4.});
    }
    EXPECT_TRUE(bond.setCashflows(cashflows).ok());
    bond.price();
    return InterestRateSwap::createBond(std::move(bond));
  }

  std::vector<ExerciseDate> schedule(std::vector<double> years) {
    std::vector<ExerciseDate> dates;
    for (double year : years) {
      // Exercising on a coupon date enters the swap after that coupon.
      dates.push_back(
          {.time_years = year, .cashflow_excluded = year > 2 ? 4. : 0.});
    }
    return dates;
  }

  ZeroSpotCurve curve_;
  ShortRateTreeCurve tree_;
};

TEST_F(BermudanSwaptionTest, OneDateIsEuropean) {
  const InterestRateSwap bond = bondFrom(2);
  InterestRateDerivative bermudan(&tree_, &bond);
  InterestRateDerivative european(&tree_, &bond);
  const VanillaOption put(100, OptionPayoff::Put);
  const auto price = bermudan.priceBermudan(put, schedule({2}));
  ASSERT_TRUE(price.ok()) << price.status();
  EXPECT_NEAR(european.price(put, 2.0), *price, 1e-12);
}

TEST_F(BermudanSwaptionTest, WorthAtLeastEveryCoterminalEuropean) {
  const InterestRateSwap bond = bondFrom(2);
  InterestRateDerivative bermudan(&tree_, &bond);
  const VanillaOption put(100, OptionPayoff::Put);
  const auto price = bermudan.priceBermudan(put, schedule({2, 3, 4, 5, 6}));
  ASSERT_TRUE(price.ok()) << price.status();

  double best_european = 0;
  for (double start : {2., 3., 4., 5., 6.}) {
    const InterestRateSwap remaining = bondFrom(start);
    InterestRateDerivative european(&tree_, &remaining);
    best_european = std::max(best_european, european.price(put, start));
  }
  EXPECT_GT(*price, best_european);
  // But not by more than the sum of all of them.
  EXPECT_LT(*price, 5 * best_european);
}

TEST_F(BermudanSwaptionTest, MoreExerciseDatesAreWorthMore) {
  const InterestRateSwap bond = bondFrom(2);
  const VanillaOption put(100, OptionPayoff::Put);
  InterestRateDerivative some(&tree_, &bond);
  InterestRateDerivative all(&tree_, &bond);
  const auto fewer = some.priceBermudan(put, schedule({2, 4}));
  const auto more = all.priceBermudan(put, schedule({2, 3, 4, 5, 6}));
  ASSERT_TRUE(fewer.ok() && more.ok());
  EXPECT_GE(*more, *fewer);
}

TEST_F(BermudanSwaptionTest, RejectsInvalidSchedules) {
  const InterestRateSwap bond = bondFrom(2);
  InterestRateDerivative bermudan(&tree_, &bond);
  const VanillaOption put(100, OptionPayoff::Put);
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            bermudan.priceBermudan(put, {}).status().code());
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            bermudan.priceBermudan(put, schedule({3, 2})).status().code());
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            bermudan.priceBermudan(put, schedule({2.1})).status().code());
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            bermudan.priceBermudan(put, schedule({20})).status().code());
}

}  // namespace smileexplorer
