#include "bsm.h"

#include <gtest/gtest.h>
namespace smileexplorer {
namespace {

TEST(BinomialTree, Derman_Page_239) {
  EXPECT_NEAR(13.75, call(300, 315, 0.2, 0.5, 0.05, 0.0), 0.005);
}

TEST(BinomialTree, Derman_VolSmile_13_3) {
  EXPECT_NEAR(33.02, call(2000, 2100, 0.16, 0.25, 0.04, 0.0), 0.005);
}

TEST(BinomialTree, Derman_VolSmile_13_4) {
  EXPECT_NEAR(21.95, call(2000, 2100, 0.16, 0.25, 0.00, 0.04), 0.005);
}

TEST(BinomialTree, Derman_VolSmile_13_5) {
  EXPECT_NEAR(26.93, call(2000, 2100, 0.16, 0.25, 0.04, 0.04), 0.005);
}

TEST(BsmTest, CurrencyOptions) {
  double spot = 140;
  double strike = 150;
  double vol = 0.2;
  double expiry = 1.0;

  double call_price = call(spot, strike, vol, expiry, 0.08, 0.04);
  double put_price = put(spot, strike, vol, expiry, 0.08, 0.04);

  EXPECT_NEAR(9.007, call_price, 0.0005);
  EXPECT_NEAR(12.964, put_price, 0.0005);

  double actual_call_delta = call_delta(spot, strike, vol, expiry, 0.08, 0.04);
  EXPECT_NEAR(0.46317, actual_call_delta, 0.000005);

  double actual_put_delta = put_delta(spot, strike, vol, expiry, 0.08, 0.04);
  EXPECT_NEAR(-0.49762, actual_put_delta, 0.000005);
}

}  // namespace
}  // namespace smileexplorer
