#include "derivative.h"

#include <gtest/gtest.h>

#include "binomial_tree.h"
#include "bsm.h"
#include "propagators.h"
#include "stochastic_tree_model.h"
#include "volatility.h"

namespace markets {
namespace {

double call_payoff(double strike, double val) {
  return std::max(0.0, val - strike);
}

double put_payoff(double strike, double val) {
  return std::max(0.0, strike - val);
}

TEST(DerivativeTest, BackpropApproxEqualsBSM) {
  // TODO: For now, this must be slightly longer because we have problems when
  // the underlying tree is exactly the length of the derivative.
  StochasticTreeModel<CRRPropagator, Volatility<FlatVol>> asset(
      BinomialTree(1.1, 1 / 360.),
      CRRPropagator(100),
      Volatility<FlatVol>(FlatVol(0.158745)));

  // TODO can we not make this copy at construction time? Then we could declare
  // the deriv anytime.
  asset.forwardPropagate();
  Derivative deriv(asset.binomialTree());

  // Verify that tree pricing is close to the BSM closed-form price.
  double bsmcall = call(100, 100, 0.158745, 1.0);
  EXPECT_NEAR(bsmcall,
              deriv.price(asset, std::bind_front(&call_payoff, 100.0), 1.0),
              0.005);

  // Verify put-call parity since this is an ATM European option.
  EXPECT_NEAR(bsmcall,
              deriv.price(asset, std::bind_front(&put_payoff, 100.0), 1.0),
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

TEST(DerivativeTest, Derman_VolSmile_13_6) {
  // This is the final part of this textbook end-of-chapter question.
  Volatility vol(DermanExampleVol{});

  StochasticTreeModel<CRRPropagator, Volatility<DermanExampleVol>> asset(
      BinomialTree(3.0, 0.1), CRRPropagator(100), vol);
  asset.forwardPropagate();

  ZeroSpotCurve curve(
      {1, 2, 3}, {0.05, 0.0747, 0.0992}, CompoundingPeriod::kAnnual);
  Derivative deriv(asset.binomialTree(), curve);

  // TODO. These tests are failing. Probably best to just print out the specific
  // components at getUpProbAt to figure out where the breakage is happening.
  EXPECT_DOUBLE_EQ(0.5238, deriv.getUpProbAt(asset, 6, 3));
  EXPECT_DOUBLE_EQ(0.5194, deriv.getUpProbAt(asset, 20, 10));
  EXPECT_DOUBLE_EQ(0.5139, deriv.getUpProbAt(asset, 50, 25));
}

}  // namespace
}  // namespace markets