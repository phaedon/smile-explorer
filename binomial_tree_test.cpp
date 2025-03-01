#include "markets/binomial_tree.h"

#include <gtest/gtest.h>

#include "binomial_tree.h"
#include "bsm.h"
#include "markets/propagators.h"
#include "markets/volatility.h"

namespace markets {
namespace {

TEST(BinomialTreeTest, Derman_VolSmile_13_1) {
  CRRPropagator crr_prop(75);

  auto walmart = BinomialTree::create(std::chrono::months(6),
                                      std::chrono::days(1),
                                      YearStyle::kBusinessDays256);
  Volatility vol(FlatVol(0.2));
  walmart.forwardPropagate(crr_prop, vol);

  // Verify to the nearest cent.
  EXPECT_NEAR(75.94, walmart.nodeValue(1, 1), 0.005);
  EXPECT_NEAR(74.07, walmart.nodeValue(1, 0), 0.005);

  EXPECT_NEAR(76.90, walmart.nodeValue(2, 2), 0.005);
  EXPECT_NEAR(75, walmart.nodeValue(2, 1), 0.005);
  EXPECT_NEAR(73.15, walmart.nodeValue(2, 0), 0.005);
}

/*
TEST(BinomialTreeTest, Derman_VolSmile_13_2) {
  JarrowRuddPropagator jr_prop(0.1, 0.2, 75);
  BinomialTree walmart(std::chrono::months(6),
                       std::chrono::days(1),
                       YearStyle::kBusinessDays256);
  walmart.forwardPropagate(jr_prop);

  // Verify to the nearest cent.
  EXPECT_NEAR(75.97, walmart.nodeValue(1, 1), 0.005);
  EXPECT_NEAR(74.10, walmart.nodeValue(1, 0), 0.005);

  EXPECT_NEAR(76.96, walmart.nodeValue(2, 2), 0.005);
  EXPECT_NEAR(75.06, walmart.nodeValue(2, 1), 0.005);
  EXPECT_NEAR(73.21, walmart.nodeValue(2, 0), 0.005);
}
*/

double call_payoff(double strike, double val) {
  return std::max(0.0, val - strike);
}

double put_payoff(double strike, double val) {
  return std::max(0.0, strike - val);
}

TEST(BinomialTree, BackwardInduction) {
  // TODO: For now, this must be slightly longer because we have problems when
  // the underlying tree is exactly the length of the derivative.
  BinomialTree asset(1.1, 1 / 360.);

  Volatility vol(FlatVol(0.158745));
  CRRPropagator crr_prop(100);
  asset.forwardPropagate(crr_prop, vol);

  BinomialTree deriv = BinomialTree::createFrom(asset);
  deriv.backPropagate(asset, std::bind_front(&call_payoff, 100.0), 1.0);
  EXPECT_NEAR(6.326, deriv.nodeValue(0, 0), 0.005);

  /*
  JarrowRuddPropagator jr_prop(0.00, 0.158745, 100);
  asset.forwardPropagate(jr_prop);
  deriv.backPropagate(
      asset, jr_prop, std::bind_front(&call_payoff, 100.0), 1.0);
  EXPECT_NEAR(6.326, deriv.nodeValue(0, 0), 0.005);
*/

  // Verify put/call parity.
  asset.forwardPropagate(crr_prop, vol);

  deriv.backPropagate(asset, std::bind_front(&call_payoff, 100.0), 1.0);
  double treecall = deriv.nodeValue(0, 0);
  double bsmcall = call(100, 100, 0.158745, 1.0);
  EXPECT_NEAR(treecall, bsmcall, 0.005);

  deriv.backPropagate(asset, std::bind_front(&put_payoff, 100.0), 1.0);
  double put = deriv.nodeValue(0, 0);
  EXPECT_NEAR(treecall, put, 0.005);
}

}  // namespace
}  // namespace markets
