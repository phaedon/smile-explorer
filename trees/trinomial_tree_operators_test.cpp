#include "trees/trinomial_tree_operators.h"

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace smileexplorer
