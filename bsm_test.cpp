#include "markets/bsm.h"

#include <gtest/gtest.h>
namespace markets {
namespace {

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

}  // namespace
}  // namespace markets
