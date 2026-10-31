#include "volatility/ssvi.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace smileexplorer {
namespace {

// An equity-index-like skew: ATM vols of 22%, 20%, 19% and 18.5% at 3 months,
// 6 months, 1 year and 2 years.
SsviSurface indexSkew(double rho = -0.7, double eta = 1.2, double gamma = 0.4) {
  const std::vector<double> t{0.25, 0.5, 1, 2};
  const std::vector<double> vols{0.22, 0.20, 0.19, 0.185};
  std::vector<double> thetas;
  for (size_t i = 0; i < t.size(); ++i)
    thetas.push_back(vols[i] * vols[i] * t[i]);
  return SsviSurface(rho, eta, gamma, t, thetas);
}

TEST(SsviTest, AtTheMoneyIsTheta) {
  const SsviSurface s = indexSkew();
  for (double t : {0.1, 0.25, 0.7, 1.0, 3.0}) {
    EXPECT_NEAR(s.theta(t), s.totalVariance(0, t), 1e-15) << t;
  }
  EXPECT_NEAR(0.19, s.impliedVol(0, 1.0), 1e-15);
  // Linear in T between expiries, and from zero before the first.
  EXPECT_NEAR(0.5 * (0.2 * 0.2 * 0.5 + 0.19 * 0.19), s.theta(0.75), 1e-15);
  EXPECT_NEAR(0.22 * 0.22 * 0.25 / 2, s.theta(0.125), 1e-15);
}

TEST(SsviTest, DerivativesMatchFiniteDifferences) {
  const SsviSurface s = indexSkew();
  const double h = 1e-5;
  for (double t : {0.3, 0.8, 1.5}) {
    for (double k : {-0.4, -0.1, 0.0, 0.2, 0.5}) {
      const auto w = [&](double kk, double tt) {
        return s.totalVariance(kk, tt);
      };
      EXPECT_NEAR((w(k + h, t) - w(k - h, t)) / (2 * h), s.dwdk(k, t), 1e-8);
      EXPECT_NEAR((w(k + h, t) - 2 * w(k, t) + w(k - h, t)) / (h * h),
                  s.d2wdk2(k, t),
                  1e-5);
      // Within a segment of theta(T), so the derivative is smooth.
      EXPECT_NEAR((w(k, t + h) - w(k, t - h)) / (2 * h), s.dwdt(k, t), 1e-7);
    }
  }
}

TEST(SsviTest, SymmetricWithoutCorrelation) {
  const SsviSurface s = indexSkew(0.0);
  for (double k : {0.1, 0.3}) {
    EXPECT_NEAR(s.totalVariance(k, 1), s.totalVariance(-k, 1), 1e-15);
  }
  // A negative rho puts more variance at low strikes: a skew.
  const SsviSurface skew = indexSkew(-0.7);
  EXPECT_GT(skew.impliedVol(-0.2, 1), skew.impliedVol(0.2, 1));
}

TEST(SsviTest, ArbitrageConditions) {
  EXPECT_TRUE(indexSkew().butterflyOk());
  EXPECT_TRUE(indexSkew().calendarOk());
  // Far too much curvature.
  EXPECT_FALSE(indexSkew(-0.9, 20).butterflyOk());
  // ATM total variance falling with expiry.
  const SsviSurface falling(-0.5, 1, 0.4, {0.5, 1}, {0.04, 0.03});
  EXPECT_FALSE(falling.calendarOk());
}

TEST(SsviTest, DensityIsADensityWithoutArbitrage) {
  const SsviSurface s = indexSkew();
  ASSERT_TRUE(s.butterflyOk());
  for (double t : {0.25, 1.0}) {
    double total = 0;
    double mean = 0;
    const double dk = 0.1;
    for (double strike = 1; strike < 400; strike += dk) {
      const double p = s.density(strike, 100, t);
      EXPECT_GE(p, 0) << strike;
      total += p * dk;
      mean += strike * p * dk;
    }
    EXPECT_NEAR(1.0, total, 1e-3) << t;
    // Undiscounted, the asset's expected value is its forward.
    EXPECT_NEAR(100, mean, 0.1) << t;
  }
}

TEST(SsviTest, ArbitrageShowsUpAsANegativeDensity) {
  const SsviSurface s = indexSkew(-0.9, 20);
  bool negative = false;
  for (double strike = 50; strike < 150; strike += 0.5) {
    negative = negative || s.density(strike, 100, 0.25) < 0;
  }
  EXPECT_TRUE(negative);
}

}  // namespace
}  // namespace smileexplorer
