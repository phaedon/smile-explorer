#include "markets/binomial_tree.h"

#include <gtest/gtest.h>

#include "binomial_tree.h"
#include "bsm.h"
#include "time.h"

namespace markets {
namespace {

TEST(BinomialTreeTest, Derman_VolSmile_13_1) {
  CRRPropagator crr_prop(0.1, 0.2, 75);
  BinomialTree walmart(std::chrono::months(6),
                       std::chrono::days(1),
                       YearStyle::kBusinessDays256);
  walmart.forwardPropagate(crr_prop);

  // Verify to the nearest cent.
  EXPECT_NEAR(75.94, walmart.nodeValue(1, 1), 0.005);
  EXPECT_NEAR(74.07, walmart.nodeValue(1, 0), 0.005);

  EXPECT_NEAR(76.90, walmart.nodeValue(2, 2), 0.005);
  EXPECT_NEAR(75, walmart.nodeValue(2, 1), 0.005);
  EXPECT_NEAR(73.15, walmart.nodeValue(2, 0), 0.005);
}

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

TEST(BinomialTree, BackwardInduction) {
  // BinomialTree asset(std::chrono::months(13),
  //                    std::chrono::days(1),
  //                    YearStyle::kBusinessDays252);
  // BinomialTree deriv(std::chrono::months(13),
  //                    std::chrono::days(1),
  //                    YearStyle::kBusinessDays252);

  BinomialTree asset(1.0, 1 / 360., YearStyle::kBusinessDays252);
  BinomialTree deriv(1.0, 1 / 360., YearStyle::kBusinessDays252);

  CRRPropagator crr_prop(0.00, 0.158745, 100);
  asset.forwardPropagate(crr_prop);
  deriv.backPropagate(
      asset, crr_prop, std::bind_front(&call_payoff, 100.0), 1.0);
  EXPECT_NEAR(6.326, deriv.nodeValue(0, 0), 0.005);

  JarrowRuddPropagator jr_prop(0.00, 0.158745, 100);
  asset.forwardPropagate(jr_prop);
  deriv.backPropagate(
      asset, jr_prop, std::bind_front(&call_payoff, 100.0), 1.0);
  EXPECT_NEAR(6.326, deriv.nodeValue(0, 0), 0.005);

  // Verify put/call parity.
  asset.forwardPropagate(crr_prop);

  deriv.backPropagate(
      asset, crr_prop, std::bind_front(&call_payoff, 100.0), 1.0);
  double treecall = deriv.nodeValue(0, 0);
  double bsmcall = call(100, 100, 0.158745, 1.0);
  EXPECT_NEAR(treecall, bsmcall, 0.005);

  deriv.backPropagate(
      asset, crr_prop, std::bind_front(&put_payoff, 100.0), 1.0);
  double put = deriv.nodeValue(0, 0);
  EXPECT_NEAR(treecall, put, 0.005);
}

TEST(BinomialTree, Derman_Page_239) {
  EXPECT_NEAR(13.75, call(300, 315, 0.2, 0.5, 0.05), 0.005);
}

TEST(BinomialTree, Derman_VolSmile_13_3) {
  EXPECT_NEAR(33.02, call(2000, 2100, 0.16, 0.25, 0.04), 0.005);
}

TEST(BinomialTree, Derman_VolSmile_13_4) {
  EXPECT_NEAR(21.95, call(2000, 2100, 0.16, 0.25, 0.00, 0.04), 0.005);
}

TEST(BinomialTree, Derman_VolSmile_13_5) {
  EXPECT_NEAR(26.93, call(2000, 2100, 0.16, 0.25, 0.04, 0.04), 0.005);
}

// Returns sig_t_T
double forwardVol(
    double t0, double t, double T, double sig_0_t, double sig_0_T) {
  return std::sqrt((T * std::pow(sig_0_T, 2) - t * std::pow(sig_0_t, 2)) /
                   (T - t));
}

double getTimeDependentVol(double t) {
  // just a hard-coded example from Derman 13-6 to get started.
  if (t <= 1) return 0.2;
  if (t <= 2) return forwardVol(0, 1, 2, 0.2, 0.255);
  return forwardVol(0, 2, 3, 0.255, 0.311);
}

TEST(BinomialTree, Derman_VolSmile_13_6) {
  BinomialTree asset(3.0, 1 / 10., YearStyle::kBusinessDays252);
  asset.resizeWithTimeDependentVol(&getTimeDependentVol);

  CRRPropagator crr_prop(0.00, 100, &getTimeDependentVol);
  asset.forwardPropagate(crr_prop);
  for (int t = 0; t < asset.numTimesteps(); ++t) {
    std::cout << "t:" << t << "   dt:" << asset.timestepAt(t) << std::endl;
  }

  // just to trigger std::cout
  EXPECT_TRUE(false);
}

}  // namespace
}  // namespace markets
