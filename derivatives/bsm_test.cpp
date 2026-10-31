#include "bsm.h"

#include <gtest/gtest.h>

#include <cmath>
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

struct BsmInputs {
  double S, K, vol, t, r, div;
};

// A spread of moneyness, expiries, rates and dividend yields.
const BsmInputs kCases[] = {
    {100, 100, 0.2, 1.0, 0.05, 0.0},
    {100, 120, 0.3, 0.5, 0.02, 0.01},
    {140, 150, 0.2, 1.0, 0.08, 0.04},
    {100, 80, 0.15, 0.05, 0.03, 0.0},
    {1.1, 1.08, 0.08, 0.25, 0.04, 0.02},
};

// Central finite difference of f(x) at x.
template <typename F>
double centralDiff(F f, double x, double h) {
  return (f(x + h) - f(x - h)) / (2 * h);
}

TEST(BsmTest, SecondOrderGreeksMatchFiniteDifferences) {
  for (const BsmInputs& c : kCases) {
    SCOPED_TRACE(testing::Message()
                 << "S=" << c.S << " K=" << c.K << " t=" << c.t);
    const double h_vol = 1e-5;
    const double h_t = 1e-6;
    const double h_s = c.S * 1e-5;
    const auto at_vol = [&](auto greek) {
      return
          [&, greek](double v) { return greek(c.S, c.K, v, c.t, c.r, c.div); };
    };
    const auto at_t = [&](auto greek) {
      return [&, greek](double t) {
        return greek(c.S, c.K, c.vol, t, c.r, c.div);
      };
    };
    const auto at_s = [&](auto greek) {
      return [&, greek](double s) {
        return greek(s, c.K, c.vol, c.t, c.r, c.div);
      };
    };
    const double vanna_value = vanna(c.S, c.K, c.vol, c.t, c.r, c.div);
    const double tol = 1e-5 * (1 + std::abs(vanna_value));

    // Vanna is d(delta)/d(vol), for calls and puts, and d(vega)/d(spot)
    // (vega above is per vol point, hence the factor 100).
    EXPECT_NEAR(
        centralDiff(at_vol(call_delta), c.vol, h_vol), vanna_value, tol);
    EXPECT_NEAR(centralDiff(at_vol(put_delta), c.vol, h_vol), vanna_value, tol);
    EXPECT_NEAR(100 * centralDiff(at_s(vega), c.S, h_s), vanna_value, tol);

    const double volga_value = volga(c.S, c.K, c.vol, c.t, c.r, c.div);
    EXPECT_NEAR(100 * centralDiff(at_vol(vega), c.vol, h_vol),
                volga_value,
                1e-5 * (1 + std::abs(volga_value)));

    // Charm and colour are per unit of calendar time, as t (time to expiry)
    // shrinks.
    const double call_charm_value =
        call_charm(c.S, c.K, c.vol, c.t, c.r, c.div);
    EXPECT_NEAR(-centralDiff(at_t(call_delta), c.t, h_t),
                call_charm_value,
                1e-4 * (1 + std::abs(call_charm_value)));
    const double put_charm_value = put_charm(c.S, c.K, c.vol, c.t, c.r, c.div);
    EXPECT_NEAR(-centralDiff(at_t(put_delta), c.t, h_t),
                put_charm_value,
                1e-4 * (1 + std::abs(put_charm_value)));
    const double colour_value = colour(c.S, c.K, c.vol, c.t, c.r, c.div);
    EXPECT_NEAR(-centralDiff(at_t(gamma), c.t, h_t),
                colour_value,
                1e-4 * (1 + std::abs(colour_value)));
  }
}

// Put delta is call delta less e^(-div t), so their charms differ by the
// rate of change of that discount factor.
TEST(BsmTest, CharmPutCallRelation) {
  for (const BsmInputs& c : kCases) {
    EXPECT_NEAR(call_charm(c.S, c.K, c.vol, c.t, c.r, c.div) -
                    put_charm(c.S, c.K, c.vol, c.t, c.r, c.div),
                c.div * std::exp(-c.div * c.t),
                1e-12);
  }
}

// Near expiry, at-the-money gamma spikes: it grows as the option ages, so
// colour is positive at the money, and negative far from it.
TEST(BsmTest, ColourSignNearExpiry) {
  EXPECT_GT(colour(100, 100, 0.2, 0.02, 0.0, 0.0), 0);
  EXPECT_LT(colour(110, 100, 0.2, 0.02, 0.0, 0.0), 0);
}

}  // namespace
}  // namespace smileexplorer
