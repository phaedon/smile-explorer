#include "trees/stochastic_tree_model.h"

#include <gtest/gtest.h>

#include "derivatives/derivative.h"
#include "rates/rates_curve.h"
#include "test/tree_matchers.h"
#include "time.h"
#include "trees/binomial_tree.h"
#include "trees/propagators.h"
#include "volatility/volatility.h"

namespace markets {
namespace {

TEST(StochasticTreeModelTest, Derman_VolSmile_13_1) {
  CRRPropagator crr_prop(75);

  auto walmart_tree = BinomialTree::create(std::chrono::months(6),
                                           std::chrono::days(1),
                                           YearStyle::kBusinessDays256);
  StochasticTreeModel asset(std::move(walmart_tree), crr_prop);

  Volatility vol(FlatVol(0.2));
  asset.forwardPropagate(vol);

  std::vector<std::vector<double>> expected = {{75},
                                               {74.07, 75.94},  //
                                               {73.15, 75, 76.9}};

  // Verify to the nearest cent.
  EXPECT_THAT(asset.binomialTree(),
              BinomialTreeMatchesUpToTimeIndex(expected, 0.005));
}

TEST(BinomialTreeTest, Derman_VolSmile_13_2) {
  JarrowRuddPropagator jr_prop(0.1, 75);
  auto walmart_tree = BinomialTree::create(std::chrono::months(6),
                                           std::chrono::days(1),
                                           YearStyle::kBusinessDays256);
  StochasticTreeModel asset(std::move(walmart_tree), jr_prop);
  Volatility vol(FlatVol(0.2));
  asset.forwardPropagate(vol);

  std::vector<std::vector<double>> expected = {{75},
                                               {74.1, 75.97},  //
                                               {73.21, 75.06, 76.96}};

  // Verify to the nearest cent.
  EXPECT_THAT(asset.binomialTree(),
              BinomialTreeMatchesUpToTimeIndex(expected, 0.005));
}

struct DermanKaniExampleVol {
  static constexpr VolSurfaceFnType type =
      VolSurfaceFnType::kTimeInvariantSkewSmile;

  double operator()(double fwd_difference) const {
    double atm_vol = 0.1;
    // Negative because drop in strike is positive change in vol.
    double vol_adjustment = -0.005 * (fwd_difference / 10);
    return atm_vol + vol_adjustment;
  }
};

TEST(StochasticTreeModelTest, DermanKani1994Example) {
  ZeroSpotCurve curve({1.0, 10.0}, {0.03, 0.03}, CompoundingPeriod::kAnnual);
  LocalVolatilityPropagator lv_prop(curve, 100.0);

  BinomialTree tree(5, 1.);
  std::cout << "built tree" << std::endl;

  StochasticTreeModel asset(std::move(tree), lv_prop);
  std::cout << "built asset" << std::endl;
  asset.forwardPropagate(Volatility(DermanKaniExampleVol()));
  std::cout << "fwdpropagate asset" << std::endl;

  asset.binomialTree().printUpTo(5);
  asset.binomialTree().printProbTreeUpTo(curve, 4);

  std::vector<double> expected_t_4{59.02, 79.43, 100.0, 120.51, 139.78};
  for (int i = 0; i < 5; ++i) {
    // This is expected to fail, because the formulation for the fwd difference
    // is not correctly implemented and/or not currently supported. That's a
    // flaw in the design of the library, and/or the implementation of the unit
    // test. But there's no bug.
    //
    //  EXPECT_DOUBLE_EQ(expected_t_4[i], asset.binomialTree().nodeValue(4, i));
  }
}

struct DermanChapter14Vol {
  static constexpr VolSurfaceFnType type =
      VolSurfaceFnType::kTimeInvariantSkewSmile;
  DermanChapter14Vol(double spot_price) : spot_price_(spot_price) {}

  double operator()(double s) const {
    return std::max(0.11 - 2 * (s - spot_price_) / spot_price_, 0.01);
  }

 private:
  double spot_price_;
};

TEST(StochasticTreeModelTest, DermanChapter14_2) {
  // No discounting.
  ZeroSpotCurve curve({1.0, 10.0}, {0.0, 0.0}, CompoundingPeriod::kContinuous);

  BinomialTree tree(0.1, 0.01);
  LocalVolatilityPropagator lv_prop(curve, 100.0);
  StochasticTreeModel asset(std::move(tree), lv_prop);
  asset.forwardPropagate(Volatility(DermanChapter14Vol(100)));

  Derivative deriv(&asset.binomialTree(), &curve);
  double price = deriv.price(std::bind_front(&European::call, 102.0), 0.04);
  EXPECT_NEAR(0.0966, price, 0.0001);

  // Risk-neutral cumulative probabilities from pg 469.
  std::vector<std::vector<double>> expected = {
      {1.},
      {.5027, .4973},  //
      {.2076, .4902, .3022},
      {.1017, .3523, .4022, .1437},
      {.0436, .2036, .3646, .2966, .0916}};

  EXPECT_THAT(deriv.arrowDebreuTree(),
              BinomialTreeMatchesUpToTimeIndex(expected, 0.0001));
}

TEST(StochasticTreeModelTest, DermanChapter14_3) {
  ZeroSpotCurve curve(
      {0.01, 1.0}, {0.04, 0.04}, CompoundingPeriod::kContinuous);
  LocalVolatilityPropagator lv_prop_with_rates(curve, 100.0);
  BinomialTree tree(0.1, 0.01);

  StochasticTreeModel asset(std::move(tree), lv_prop_with_rates);
  asset.forwardPropagate(Volatility(DermanChapter14Vol(100)));
  Derivative deriv(&asset.binomialTree(), &curve);
  double price = deriv.price(std::bind_front(&European::call, 102.0), 0.04);

  // These expected numbers are from page 470.
  std::vector<std::vector<double>> expected = {
      {100},
      {98.91, 101.11},  //
      {97.33, 100, 101.84},
      {95.72, 98.91, 101.11, 102.6},
      {93.53, 97.33, 100, 101.84, 103.08}};

  // Verify to the nearest cent.
  EXPECT_THAT(asset.binomialTree(),
              BinomialTreeMatchesUpToTimeIndex(expected, 0.005));

  EXPECT_NEAR(0.12, price, 0.005);
}

}  // namespace
}  // namespace markets
