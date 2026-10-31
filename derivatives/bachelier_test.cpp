#include "derivatives/bachelier.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "rates/swap_rates.h"
#include "rates/zero_curve.h"

namespace smileexplorer {
namespace {

TEST(BachelierTest, AtTheMoney) {
  // At the money, the price is the stddev times 1 / sqrt(2 pi).
  const double stddev = 0.01 * std::sqrt(2.0);
  EXPECT_NEAR(stddev / std::sqrt(2 * std::numbers::pi),
              bachelier(0.03, 0.03, 0.01, 2.0, true),
              1e-15);
  EXPECT_DOUBLE_EQ(bachelier(0.03, 0.03, 0.01, 2.0, true),
                   bachelier(0.03, 0.03, 0.01, 2.0, false));
}

TEST(BachelierTest, PutCallParity) {
  for (double strike : {0.01, 0.03, 0.05}) {
    EXPECT_NEAR(0.03 - strike,
                bachelier(0.03, strike, 0.008, 1.5, true) -
                    bachelier(0.03, strike, 0.008, 1.5, false),
                1e-15);
  }
}

TEST(BachelierTest, ZeroVolIsIntrinsic) {
  EXPECT_DOUBLE_EQ(0.01, bachelier(0.04, 0.03, 0, 1, true));
  EXPECT_DOUBLE_EQ(0.0, bachelier(0.04, 0.03, 0, 1, false));
}

TEST(BachelierTest, ImpliedNormalVolRoundTrips) {
  for (double strike : {0.0, 0.02, 0.03, 0.045}) {
    for (bool is_call : {true, false}) {
      const double price = bachelier(0.03, strike, 0.0085, 3, is_call);
      const auto vol = impliedNormalVol(price, 0.03, strike, 3, is_call);
      ASSERT_TRUE(vol.has_value());
      EXPECT_NEAR(0.0085, *vol, 1e-12);
    }
  }
}

TEST(BachelierTest, ImpliedNormalVolRejectsImpossiblePrices) {
  // Below intrinsic value, or at it, no vol produces the price.
  EXPECT_FALSE(impliedNormalVol(0.009, 0.04, 0.03, 1, true).has_value());
  EXPECT_FALSE(impliedNormalVol(0.0, 0.03, 0.03, 1, true).has_value());
  EXPECT_FALSE(impliedNormalVol(NAN, 0.03, 0.03, 1, true).has_value());
  EXPECT_FALSE(impliedNormalVol(0.01, 0.03, 0.03, 0, true).has_value());
}

TEST(SwapRatesTest, ForwardAnnuityAndParRate) {
  const ZeroSpotCurve curve({1, 2, 5}, {0.03, 0.035, 0.04});
  EXPECT_NEAR(std::exp(0.03) - 1, simpleForwardRate(curve, 0, 1), 1e-12);
  const std::vector<double> payments{2, 3, 4};
  EXPECT_NEAR(curve.df(2) + curve.df(3) + curve.df(4),
              annuity(curve, 1, payments),
              1e-12);
  // At the par rate, the fixed leg is worth the floating leg.
  const double par = parSwapRate(curve, 1, payments);
  EXPECT_NEAR(
      curve.df(1) - curve.df(4), par * annuity(curve, 1, payments), 1e-12);
}

}  // namespace
}  // namespace smileexplorer
