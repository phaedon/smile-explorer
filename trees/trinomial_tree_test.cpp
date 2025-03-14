#include "trees/trinomial_tree.h"

#include <gtest/gtest.h>

#include "gmock/gmock.h"

namespace smileexplorer {
namespace {

using testing::AllOf;
using testing::DoubleNear;
using testing::Field;

TEST(BranchProbabilitiesTest, Hull_FirstStage) {
  double sigma = 0.01;
  double dt = 1.0;
  TrinomialTree tree(6, 0.1, dt, sigma);
  tree.firstStage();

  // Verify a couple of nodes from the table in Hull, pg 741 (or Hull & White
  // 1994, Exhibit 2)
  // TODO: Create a custom matcher for the TrimonialNode.

  const auto& node_E = tree.tree_[2][4];
  EXPECT_THAT(
      node_E.branch_probs,
      AllOf(Field(&BranchProbabilities::pu, DoubleNear(0.8867, 0.00005)),
            Field(&BranchProbabilities::pm, DoubleNear(0.0267, 0.00005)),
            Field(&BranchProbabilities::pd, DoubleNear(0.0867, 0.00005))));
  EXPECT_NEAR(0.03464, tree.shortRate(2, 4), 0.00001);

  const auto& node_H = tree.tree_[2][1];
  EXPECT_THAT(
      node_H.branch_probs,
      AllOf(Field(&BranchProbabilities::pu, DoubleNear(0.2217, 0.00005)),
            Field(&BranchProbabilities::pm, DoubleNear(0.6567, 0.00005)),
            Field(&BranchProbabilities::pd, DoubleNear(0.1217, 0.00005))));
  EXPECT_NEAR(-0.01732, tree.shortRate(2, 1), 0.00001);

  // Verify truncation.
  std::vector<double> expected_num_nodes_per_timeslice{1, 3, 5, 5};
  for (size_t i = 0; i < expected_num_nodes_per_timeslice.size(); ++i) {
    EXPECT_EQ(expected_num_nodes_per_timeslice[i], tree.tree_[i].size());
  }
}

TEST(BranchProbabilitiesTest, Hull_SecondStage) {
  ZeroSpotCurve market_curve(
      {0.5, 1.0, 1.5, 2.0, 2.5, 3.0},
      {.0343, .03824, 0.04183, 0.04512, 0.04812, 0.05086});

  double sigma = 0.01;
  double dt = 1.0;
  TrinomialTree tree(1.25, 0.1, dt, sigma);
  tree.forwardPropagate(market_curve);
  EXPECT_THAT(
      std::vector<double>(tree.alphas_.begin(), tree.alphas_.begin() + 3),
      testing::ElementsAre(DoubleNear(0.03824, 0.00001),
                           DoubleNear(0.05205, 0.00001),
                           DoubleNear(0.06252, 0.00001)));
}

}  // namespace
}  // namespace smileexplorer
