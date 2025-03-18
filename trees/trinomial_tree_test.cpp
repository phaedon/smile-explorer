#include "trees/trinomial_tree.h"

#include <gtest/gtest.h>

#include "gmock/gmock.h"

namespace smileexplorer {
namespace {

using testing::DoubleNear;

TEST(BranchProbabilitiesTest, Hull_SecondStage) {
  ZeroSpotCurve market_curve(
      {0.5, 1.0, 1.5, 2.0, 2.5, 3.0},
      {.0343, .03824, 0.04183, 0.04512, 0.04812, 0.05086});

  double sigma = 0.01;
  double dt = 1.0;
  size_t num_elems_to_verify = 3;
  TrinomialTree tree(num_elems_to_verify * dt, 0.1, dt, sigma);
  tree.forwardPropagate(market_curve);
  EXPECT_THAT(std::vector<double>(tree.alphas_.begin(),
                                  tree.alphas_.begin() + num_elems_to_verify),
              testing::ElementsAre(DoubleNear(0.03824, 0.00001),
                                   DoubleNear(0.05205, 0.00001),
                                   DoubleNear(0.06252, 0.00001)));
}

}  // namespace
}  // namespace smileexplorer
