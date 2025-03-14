#include "derivatives/derivative.h"

#include <gtest/gtest.h>

#include "rates/zero_curve.h"
#include "trees/binomial_tree.h"
#include "trees/propagators.h"
#include "trees/stochastic_tree_model.h"
#include "vanilla_option.h"
#include "volatility/volatility.h"

namespace smileexplorer {
namespace {

TEST(DerivativeTest, TreePricingApproxEqualsBSM) {
  // TODO: For now, this must be slightly longer because we have problems when
  // the underlying tree is exactly the length of the derivative.
  StochasticTreeModel<CRRPropagator> asset(BinomialTree(1.1, 1 / 360.),
                                           CRRPropagator(100));
  Volatility flat_vol(FlatVol(0.158745));
  asset.forwardPropagate(flat_vol);
  NoDiscountingCurve no_curve;
  SingleAssetDerivative deriv(&asset.binomialTree(), &no_curve);

  // Verify that tree pricing is close to the BSM closed-form price.
  // double bsmcall = call(100, 100, 0.158745, 1.0);
  VanillaOption vanilla_call(100, OptionPayoff::Call);
  EXPECT_NEAR(vanilla_call.blackScholes(100, 0.158745, 1.0, 0.0, 0.0),
              deriv.price(vanilla_call, 1.0),
              0.005);

  // Verify put-call parity since this is an ATM European option.
  EXPECT_NEAR(vanilla_call.blackScholes(100, 0.158745, 1.0, 0.0, 0.0),
              deriv.price(VanillaOption(100, OptionPayoff::Put), 1.0),
              0.005);

  // Verify that tree pricing matches BSM for an OTM option with discounting.
  const double disc_rate = 0.12;
  ZeroSpotCurve curve({1.0, 10.0}, {disc_rate, disc_rate});
  vanilla_call = VanillaOption(105, OptionPayoff::Call);
  double bsm_otm_discounting =
      vanilla_call.blackScholes(100, 0.158745, 1.0, disc_rate, 0.0);
  deriv = SingleAssetDerivative(&asset.binomialTree(), &curve);
  EXPECT_NEAR(bsm_otm_discounting,
              deriv.price(VanillaOption(105, OptionPayoff::Call), 1.0),
              0.005);

  // Verify that forward-propagation method doesn't affect the deriv price at
  // the limit.
  StochasticTreeModel<JarrowRuddPropagator> jrasset(
      BinomialTree(1.1, 1 / 360.), JarrowRuddPropagator(0.1, 100));
  jrasset.forwardPropagate(flat_vol);
  SingleAssetDerivative jrderiv(&jrasset.binomialTree(), &curve);
  EXPECT_NEAR(jrderiv.price(VanillaOption(105, OptionPayoff::Call), 1.0),
              deriv.price(VanillaOption(105, OptionPayoff::Call), 1.0),
              0.005);
}

struct DermanExampleVol {
  static constexpr VolSurfaceFnType type = VolSurfaceFnType::kTermStructure;
  double operator()(double t) const {
    if (t <= 1) return 0.2;
    if (t <= 2) return forwardVol(0, 1, 2, 0.2, 0.255);
    return forwardVol(0, 2, 3, 0.255, 0.311);
  }
};

// TODO Move this out of this module.
TEST(DerivativeTest, Derman_VolSmile_13_6) {
  // This is the final part of this textbook end-of-chapter question.
  Volatility vol(DermanExampleVol{});

  StochasticTreeModel<CRRPropagator> asset(BinomialTree(3.0, 0.1),
                                           CRRPropagator(100));
  asset.forwardPropagate(vol);

  ZeroSpotCurve curve(
      {1, 2, 3}, {0.05, 0.0747, 0.0992}, CompoundingPeriod::kContinuous);

  EXPECT_NEAR(0.5238, asset.binomialTree().getUpProbAt(curve, 6, 3), 0.0005);
  EXPECT_NEAR(0.5194, asset.binomialTree().getUpProbAt(curve, 20, 10), 0.0005);
  EXPECT_NEAR(0.5139, asset.binomialTree().getUpProbAt(curve, 50, 25), 0.0005);
}

TEST(DerivativeTest, VerifySubscriptionMechanism) {
  StochasticTreeModel<CRRPropagator> asset(BinomialTree(1.1, 1 / 360.),
                                           CRRPropagator(100));
  Volatility flat_vol(FlatVol(0.158745));
  asset.forwardPropagate(flat_vol);
  NoDiscountingCurve no_curve;

  SingleAssetDerivative deriv(&asset.binomialTree(), &no_curve);

  double price0 = deriv.price(VanillaOption(100, OptionPayoff::Call), 1.0);
  asset.forwardPropagate(Volatility(FlatVol{0.25}));
  double price1 = deriv.price(VanillaOption(100, OptionPayoff::Call), 1.0);
  EXPECT_LT(price0, price1);

  // It's a bit annoying that you have to call forwardPropagate manually here,
  // but this is because it's the only place where the volatility is provided.
  // It's just an ephemeral thing that triggers the diffusion. In the future we
  // could also treat spot the same way and not bake it into the propagator.
  asset.updateSpot(90);
  asset.forwardPropagate(Volatility(FlatVol{0.25}));
  double price2 = deriv.price(VanillaOption(100, OptionPayoff::Call), 1.0);
  EXPECT_LT(price2, price1);
}

TEST(DerivativeTest, CurrencyOptionTreePricing) {
  // Example based on Figure 13.12 on pg. 306 of John Hull, "Options,
  // Futures..."
  ZeroSpotCurve domestic_curve(
      {1, 3}, {0.05, 0.05}, CompoundingPeriod::kContinuous);
  ZeroSpotCurve foreign_curve(
      {1, 3}, {0.07, 0.07}, CompoundingPeriod::kContinuous);

  StochasticTreeModel<CRRPropagator> asset(BinomialTree(.5, 1 / 12.),
                                           CRRPropagator(0.61));
  Volatility flat_vol(FlatVol(0.12));
  asset.forwardPropagate(flat_vol);

  CurrencyDerivative fxderiv(
      &asset.binomialTree(), &domestic_curve, &foreign_curve);

  // This is the European equivalent.
  double option_price =
      fxderiv.price(VanillaOption(0.6, OptionPayoff::Call), 0.25);
  EXPECT_NEAR(0.01860, option_price, 0.00001);

  // This is the version in Hull.
  double amer_option_price = fxderiv.price(
      VanillaOption(0.6, OptionPayoff::Call, ExerciseStyle::American), 0.25);
  EXPECT_NEAR(0.01888, amer_option_price, 0.00001);
}

}  // namespace
}  // namespace smileexplorer