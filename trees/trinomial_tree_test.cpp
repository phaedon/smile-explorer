#include "trees/trinomial_tree.h"

#include <gtest/gtest.h>

namespace smileexplorer {
namespace {

TEST(BranchProbabilitiesTest, Hull_FirstStage) {
  double a = 0.1;
  double dt = 1.0;
  double sigma = 0.01;

  int jmax = j_max(a, dt);
  int jmin = -jmax;

  auto node_E =
      BranchProbabilities(a, 2, dt, TrinomialBranchStyle::SlantedDown);
  double node_E_R_star = 2 * dR(sigma, dt);

  // Verify a couple of nodes from the table in Hull, pg 741 (or Hull & White
  // 1994, Exhibit 2)
  // TODO: Create a custom matcher for BranchProbabilities or better yet for the
  // TrimonialNode.
  EXPECT_NEAR(0.8867, node_E.pu, 0.00005);
  EXPECT_NEAR(0.0267, node_E.pm, 0.00005);
  EXPECT_NEAR(0.0867, node_E.pd, 0.00005);
  EXPECT_NEAR(0.03464, node_E_R_star, 0.00001);

  auto node_H = BranchProbabilities(a, -1, dt, TrinomialBranchStyle::Centered);
  double node_H_R_star = -1 * dR(sigma, dt);

  EXPECT_NEAR(0.2217, node_H.pu, 0.00005);
  EXPECT_NEAR(0.6567, node_H.pm, 0.00005);
  EXPECT_NEAR(0.1217, node_H.pd, 0.00005);
  EXPECT_NEAR(-0.01732, node_H_R_star, 0.00001);
}

}  // namespace
}  // namespace smileexplorer
