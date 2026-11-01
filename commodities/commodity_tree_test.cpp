#include "commodities/commodity_tree.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

#include "commodities/futures_curve.h"
#include "derivatives/bsm.h"

namespace smileexplorer {
namespace {

// A seasonal curve shaped like natural gas: monthly contracts for two years,
// peaking each January.
FuturesCurve seasonalCurve() {
  std::vector<double> times, prices;
  for (int m = 1; m <= 24; ++m) {
    const double t = m / 12.0;
    times.push_back(t);
    prices.push_back(
        3.0 * std::exp(0.02 * t) *
        (1 + 0.25 * std::cos(2 * std::numbers::pi * (t - 2.0 / 12))));
  }
  return FuturesCurve(3.2, times, prices);
}

constexpr double kA = 1.5;
constexpr double kSigma = 0.6;

TEST(CommodityTreeTest, ExpectedSpotMatchesTheCurveAtEverySlice) {
  const FuturesCurve curve = seasonalCurve();
  const CommodityTree tree(curve, kA, kSigma, 1.0 / 48, 2.0);
  ASSERT_EQ(tree.numSlices(), 97);
  for (int ti = 0; ti < tree.numSlices(); ++ti) {
    const double f = curve.price(tree.time(ti));
    EXPECT_NEAR(tree.expectedSpot(ti) / f, 1.0, 1e-12) << "slice " << ti;
  }
  EXPECT_DOUBLE_EQ(tree.spot(0, 0), 3.2);
  EXPECT_DOUBLE_EQ(tree.alpha(0), std::log(3.2));
}

TEST(CommodityTreeTest, ProbabilitiesSumToOne) {
  const CommodityTree tree(seasonalCurve(), kA, kSigma, 1.0 / 48, 1.0);
  for (int ti = 0; ti < tree.numSlices(); ++ti) {
    double total = 0.0;
    for (int i = 0; i < tree.numStates(ti); ++i) {
      EXPECT_GE(tree.probability(ti, i), 0.0);
      total += tree.probability(ti, i);
    }
    EXPECT_NEAR(total, 1.0, 1e-12);
  }
}

TEST(CommodityTreeTest, StopsWideningAtJMax) {
  const CommodityTree tree(seasonalCurve(), kA, kSigma, 1.0 / 48, 1.0);
  // ceil(0.184 / (1.5 / 48)) = 6.
  EXPECT_EQ(tree.jMax(), 6);
  EXPECT_EQ(tree.numStates(tree.numSlices() - 1), 13);
  EXPECT_DOUBLE_EQ(tree.dx(), kSigma * std::sqrt(3.0 / 48));
  EXPECT_DOUBLE_EQ(tree.x(1, 0), -tree.dx());
  // The middle successor of the root is the middle of slice 1.
  EXPECT_EQ(tree.successorMid(0, 0), 1);
}

TEST(CommodityTreeTest, FuturesAtTheRootIsTheCurve) {
  const FuturesCurve curve = seasonalCurve();
  const CommodityTree tree(curve, kA, kSigma, 1.0 / 48, 1.0);
  for (int delivery : {4, 12, 31, 48}) {
    const auto futures = tree.futuresValues(delivery);
    ASSERT_EQ(std::ssize(futures), delivery + 1);
    EXPECT_NEAR(futures[0][0] / curve.price(delivery / 48.0), 1.0, 1e-12);
    // At delivery, the futures price is the spot.
    EXPECT_DOUBLE_EQ(futures[delivery][0], tree.spot(delivery, 0));
  }
}

TEST(CommodityTreeTest, NearFuturesMoveMoreThanFarOnes) {
  const CommodityTree tree(seasonalCurve(), kA, kSigma, 1.0 / 48, 2.0);
  // One step in: the spread of the futures over the three nodes, relative to
  // its level, shrinks with the time to delivery.
  const auto spread = [&](int delivery) {
    const auto f = tree.futuresValues(delivery);
    return std::log(f[1][2] / f[1][0]);
  };
  EXPECT_GT(spread(12), spread(48));
  EXPECT_GT(spread(48), spread(96));
}

double black76(
    double f, double k, double vol, double t, double r, bool is_call) {
  return is_call ? call(f, k, vol, t, r, r) : put(f, k, vol, t, r, r);
}

// The tree's European option on futures, against Black-76 with the
// Samuelson vol, at a given number of steps a month.
double europeanError(int steps_per_month,
                     int expiry_month,
                     int delivery_month,
                     double strike,
                     bool is_call) {
  const FuturesCurve curve = seasonalCurve();
  const double dt = 1.0 / (12 * steps_per_month);
  const CommodityTree tree(curve, kA, kSigma, dt, delivery_month / 12.0);
  const double r = 0.04;
  const auto values =
      tree.optionOnFutures({.delivery_index = delivery_month * steps_per_month,
                            .expiry_index = expiry_month * steps_per_month,
                            .strike = strike,
                            .is_call = is_call},
                           r);
  const double t0 = expiry_month / 12.0;
  const double T = delivery_month / 12.0;
  const double analytic = black76(curve.price(T),
                                  strike,
                                  samuelsonImpliedVol(kA, kSigma, t0, T),
                                  t0,
                                  r,
                                  is_call);
  EXPECT_NEAR(values.price, values.european_price, 1e-15);
  return std::abs(values.price - analytic) / analytic;
}

TEST(CommodityTreeTest, EuropeanMatchesBlack76WithTheSamuelsonVol) {
  // A call on the February future expiring at delivery, a put on a summer
  // contract expiring three months before, and a short-dated call on a
  // contract more than a year out. The tree's error oscillates as the strike
  // moves between nodes, so check it at a fine step rather than its trend.
  for (const auto& [expiry, delivery, strike, is_call] :
       std::vector<std::tuple<int, int, double, bool>>{
           {3, 3, 3.9, true}, {6, 9, 3.0, false}, {2, 14, 3.4, true}}) {
    EXPECT_LT(europeanError(16, expiry, delivery, strike, is_call), 0.006)
        << expiry << " " << delivery;
  }
  // Two steps a month is visibly coarse.
  EXPECT_GT(europeanError(2, 3, 3, 3.9, true), 0.05);
}

TEST(CommodityTreeTest, AmericanPutIsWorthMoreWhenRatesArePositive) {
  const CommodityTree tree(seasonalCurve(), kA, kSigma, 1.0 / 48, 1.0);
  // Deep in the money: the holder exercises early to earn interest on the
  // payoff.
  const auto values = tree.optionOnFutures({.delivery_index = 36,
                                            .expiry_index = 36,
                                            .strike = 6.0,
                                            .is_call = false,
                                            .is_american = true},
                                           0.1);
  EXPECT_GT(values.price, values.european_price + 1e-3);
  EXPECT_TRUE(values.exercised[0][0]);
  EXPECT_DOUBLE_EQ(values.price, 6.0 - values.futures[0][0]);
  ASSERT_EQ(std::ssize(values.values), 37);
  EXPECT_EQ(values.values[36].size(), 13u);
}

TEST(CommodityTreeTest, AmericanCallIsEuropeanAtZeroRates) {
  const CommodityTree tree(seasonalCurve(), kA, kSigma, 1.0 / 48, 1.0);
  const auto values = tree.optionOnFutures({.delivery_index = 40,
                                            .expiry_index = 30,
                                            .strike = 2.5,
                                            .is_call = true,
                                            .is_american = true},
                                           0.0);
  EXPECT_NEAR(values.price, values.european_price, 1e-12);
  for (int ti = 0; ti < 30; ++ti) {
    for (bool exercised : values.exercised[ti]) EXPECT_FALSE(exercised);
  }
  ASSERT_EQ(std::ssize(values.futures), 31);
}

TEST(CommodityTreeTest, ExpiringTodayIsIntrinsic) {
  const FuturesCurve curve = seasonalCurve();
  const CommodityTree tree(curve, kA, kSigma, 1.0 / 48, 1.0);
  const auto values = tree.optionOnFutures(
      {.delivery_index = 12, .expiry_index = 0, .strike = 3.0}, 0.04);
  EXPECT_NEAR(values.price, curve.price(0.25) - 3.0, 1e-12);
  EXPECT_TRUE(values.exercised[0][0]);
}

TEST(SamuelsonTest, FuturesVolDecaysWithTimeToDelivery) {
  EXPECT_DOUBLE_EQ(instantaneousFuturesVol(kA, kSigma, 0.0), kSigma);
  EXPECT_DOUBLE_EQ(instantaneousFuturesVol(kA, kSigma, 1.0),
                   kSigma * std::exp(-kA));
}

TEST(SamuelsonTest, ImpliedVolWithoutMeanReversionIsSigma) {
  EXPECT_DOUBLE_EQ(samuelsonImpliedVol(0.0, kSigma, 0.5, 1.0), kSigma);
  EXPECT_NEAR(samuelsonImpliedVol(1e-9, kSigma, 0.5, 1.0), kSigma, 1e-9);
}

TEST(SamuelsonTest, ImpliedVolAveragesTheVarianceOverTheOptionsLife) {
  // Expiring at delivery: sigma^2 (1 - exp(-2aT)) / (2aT).
  const double T = 0.5;
  EXPECT_NEAR(samuelsonImpliedVol(kA, kSigma, T, T),
              kSigma * std::sqrt((1 - std::exp(-2 * kA * T)) / (2 * kA * T)),
              1e-14);
  // An option on a later contract is calmer still.
  EXPECT_LT(samuelsonImpliedVol(kA, kSigma, T, 1.0),
            samuelsonImpliedVol(kA, kSigma, T, T));
}

}  // namespace
}  // namespace smileexplorer
