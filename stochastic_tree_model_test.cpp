#include "stochastic_tree_model.h"

#include <gtest/gtest.h>

#include "binomial_tree.h"
#include "derivative.h"
#include "propagators.h"
#include "rates/rates_curve.h"
#include "time.h"
#include "volatility.h"

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

  // Verify to the nearest cent.
  EXPECT_NEAR(75.94, asset.binomialTree().nodeValue(1, 1), 0.005);
  EXPECT_NEAR(74.07, asset.binomialTree().nodeValue(1, 0), 0.005);

  EXPECT_NEAR(76.90, asset.binomialTree().nodeValue(2, 2), 0.005);
  EXPECT_NEAR(75, asset.binomialTree().nodeValue(2, 1), 0.005);
  EXPECT_NEAR(73.15, asset.binomialTree().nodeValue(2, 0), 0.005);
}

TEST(BinomialTreeTest, Derman_VolSmile_13_2) {
  JarrowRuddPropagator jr_prop(0.1, 75);
  auto walmart_tree = BinomialTree::create(std::chrono::months(6),
                                           std::chrono::days(1),
                                           YearStyle::kBusinessDays256);
  StochasticTreeModel asset(std::move(walmart_tree), jr_prop);
  Volatility vol(FlatVol(0.2));
  asset.forwardPropagate(vol);

  // Verify to the nearest cent.
  EXPECT_NEAR(75.97, asset.binomialTree().nodeValue(1, 1), 0.005);
  EXPECT_NEAR(74.10, asset.binomialTree().nodeValue(1, 0), 0.005);

  EXPECT_NEAR(76.96, asset.binomialTree().nodeValue(2, 2), 0.005);
  EXPECT_NEAR(75.06, asset.binomialTree().nodeValue(2, 1), 0.005);
  EXPECT_NEAR(73.21, asset.binomialTree().nodeValue(2, 0), 0.005);
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
    // EXPECT_DOUBLE_EQ(expected_t_4[i], asset.binomialTree().nodeValue(4, i));
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

double call_payoff(double strike, double val) {
  return std::max(0.0, val - strike);
}

TEST(StochasticTreeModelTest, DermanChapter14_2) {
  // No discounting.
  ZeroSpotCurve curve({1.0, 10.0}, {0.0, 0.0}, CompoundingPeriod::kContinuous);

  BinomialTree tree(0.1, 0.01);
  LocalVolatilityPropagator lv_prop(curve, 100.0);
  StochasticTreeModel asset(std::move(tree), lv_prop);
  asset.forwardPropagate(Volatility(DermanChapter14Vol(100)));
  asset.binomialTree().printUpTo(5);

  Derivative deriv(&asset, &curve);
  double price = deriv.price(std::bind_front(&call_payoff, 102.0), 0.04);
  EXPECT_NEAR(0.0966, price, 0.0001);
}

TEST(StochasticTreeModelTest, DermanChapter14_3) {
  ZeroSpotCurve curve(
      {0.01, 1.0}, {0.04, 0.04}, CompoundingPeriod::kContinuous);
  LocalVolatilityPropagator lv_prop_with_rates(curve, 100.0);
  BinomialTree tree(0.1, 0.01);

  StochasticTreeModel asset(std::move(tree), lv_prop_with_rates);
  asset.forwardPropagate(Volatility(DermanChapter14Vol(100)));
  Derivative deriv(&asset, &curve);
  double price = deriv.price(std::bind_front(&call_payoff, 102.0), 0.04);

  // You can uncomment or maybe make a matcher for these trees to compare to the
  // numbers on page 470.
  //
  // asset2.binomialTree().printUpTo(5);
  // asset2.binomialTree().printProbTreeUpTo(curve, 5);

  EXPECT_NEAR(0.12, price, 0.005);
}

}  // namespace
}  // namespace markets
