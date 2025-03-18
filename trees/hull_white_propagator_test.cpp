#include "trees/hull_white_propagator.h"

#include <gtest/gtest.h>

#include "gmock/gmock.h"
#include "trinomial_tree.h"

namespace smileexplorer {
namespace {

using testing::AllOf;
using testing::DoubleNear;
using testing::Field;

TEST(BranchProbabilitiesTest, HullWhite_FirstStage) {
  double dt = 1.0;
  double sigma = 0.01;

  HullWhitePropagator hw_prop(0.1, sigma, dt);
  TrinomialTree tree(6, dt);

  // This is essentially the "first stage" of Hull-White. It's a simple loop so
  // we repeat it here in order to avoid a dependency on the ShortRateTreeCurve
  // class.
  for (size_t ti = 0; ti < tree.tree_.size(); ++ti) {
    for (int state_index = 0; state_index < hw_prop.numStatesAtTimeIndex(ti);
         ++state_index) {
      tree.tree_[ti].push_back(hw_prop(ti, state_index));
    }
  }

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

}  // namespace
}  // namespace smileexplorer
