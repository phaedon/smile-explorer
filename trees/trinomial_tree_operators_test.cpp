#include "trees/trinomial_tree_operators.h"

#include <gtest/gtest.h>

#include "rates/short_rate_tree_curve.h"
#include "trinomial_tree.h"

namespace smileexplorer {
namespace {

TEST(TrinomialTreeOperatorsTest, MatchingTrees) {
  // Create 4 trees, all of which differ by a small parameter change:
  auto a = TrinomialTree::create(5, ForwardRateTenor::k6Month, 17);
  auto b = TrinomialTree::create(5.1, ForwardRateTenor::k6Month, 17);
  auto c = TrinomialTree::create(5, ForwardRateTenor::k3Month, 17);
  auto d = TrinomialTree::create(5, ForwardRateTenor::k3Month, 18);

  EXPECT_FALSE(treesHaveMatchingStructure(a, b));
  EXPECT_FALSE(treesHaveMatchingStructure(b, c));
  EXPECT_FALSE(treesHaveMatchingStructure(c, d));
  EXPECT_FALSE(treesHaveMatchingStructure(a, d));

  EXPECT_TRUE(treesHaveMatchingStructure(a, TrinomialTree::createFrom(a)));
  EXPECT_TRUE(treesHaveMatchingStructure(b, TrinomialTree::createFrom(b)));
}

TEST(TrinomialTreeOperatorsTest, HullWhiteParamsMayCauseTreeMismatches) {
  const double dt = 0.25;
  ZeroSpotCurve market_curve({1, 2, 5, 10}, {3, 4, 5, 6});

  // Changing the mean reversion param changes the number of states in some of
  // the timeslices. Therefore, the trees do not match.
  ShortRateTreeCurve a(
      std::make_unique<HullWhitePropagator>(.1, .01, dt), market_curve, 10);
  ShortRateTreeCurve b(
      std::make_unique<HullWhitePropagator>(.15, .01, dt), market_curve, 10);

  EXPECT_FALSE(
      treesHaveMatchingStructure(a.trinomialTree(), b.trinomialTree()));

  // However, simply bumping up the volatility doesn't change the underlying
  // structure.
  ShortRateTreeCurve c(
      std::make_unique<HullWhitePropagator>(.15, .02, dt), market_curve, 10);
  EXPECT_TRUE(treesHaveMatchingStructure(b.trinomialTree(), c.trinomialTree()));
}

}  // namespace
}  // namespace smileexplorer
