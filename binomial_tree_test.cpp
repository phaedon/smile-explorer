#include "markets/binomial_tree.h"

#include <gtest/gtest.h>

namespace markets {
namespace {
TEST(BinomialTreeTest, Years) {
  auto timestep = std::chrono::weeks(5);
  BinomialTree tree(std::chrono::years(2), timestep);
  tree.setInitValue(5.0);
  tree.populateTreeForward(genUpFn(0.15, timestep), genDownFn(0.15, timestep));
  tree.print();
  EXPECT_EQ(1, 1);
}

TEST(BinomialTreeTest, Months) {
  auto timestep = std::chrono::days(2);
  BinomialTree tree(std::chrono::months(2), timestep);
  tree.setInitValue(5.0);
  tree.populateTreeForward(genUpFn(0.15, timestep), genDownFn(0.15, timestep));
  tree.print();
  EXPECT_EQ(1, 1);
}

}  // namespace
}  // namespace markets
