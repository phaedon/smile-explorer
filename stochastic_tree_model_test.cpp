#include "stochastic_tree_model.h"

#include <gtest/gtest.h>

#include "binomial_tree.h"
#include "propagators.h"

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

}  // namespace
}  // namespace markets
