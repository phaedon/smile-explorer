#include "markets/binomial_tree.h"

#include <gtest/gtest.h>

#include "binomial_tree.h"
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

}  // namespace
}  // namespace markets
