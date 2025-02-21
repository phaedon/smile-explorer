#include "markets/volatility.h"

#include <gtest/gtest.h>

namespace markets {

struct TermStrucVol {
  double operator()(double t) const { return 1.1 * t; }
};

struct FlatTermStrucVol {
  double operator()(double t) const { return 0.15; }
};

struct VolSurfaceFn {
  double operator()(double t, double s) const { return t + s; }
};

namespace {

TEST(VolatilityTest, FlatVol) {
  Volatility vol(FlatVol(0.15));
  EXPECT_DOUBLE_EQ(0.15, vol.get());
}

TEST(VolatilityTest, TermStrucVol) {
  TermStrucVol tsm;
  Volatility vol(tsm);
  EXPECT_DOUBLE_EQ(0.11, vol.get(0.1));
}

TEST(VolatilityTest, VolSurface) {
  VolSurfaceFn volfn;
  Volatility vol(volfn);

  EXPECT_DOUBLE_EQ(3, vol.get(1, 2));
}

TEST(VolatilityTest, ConstantTimeGrid) {
  Volatility vol(FlatVol(0.15));
  auto timegrid = vol.getTimegrid(3.0, 0.3);
  ASSERT_EQ(11, timegrid.size());
  // The maturity is an exact multiple of the timestep.
  EXPECT_DOUBLE_EQ(3.0, timegrid[timegrid.size() - 1]);

  // If the maturity is not an exact multiple, ensure that
  // the timegrid extends past the end of the desired maturity.
  timegrid = vol.getTimegrid(3.0, 0.4);
  ASSERT_EQ(9, timegrid.size());
  EXPECT_DOUBLE_EQ(3.2, timegrid[timegrid.size() - 1]);
}

TEST(VolatilityTest, TimeVaryingGridMatchesFlatVol) {
  // We use FlatTermStrucVol to ensure that the time-varying
  // implementation matches that of the FlatVol in the simplest case.
  FlatTermStrucVol tsm;
  Volatility vol(tsm);
  Volatility flatvol(FlatVol(0.15));

  const double initial_timestep = 0.3;
  auto timegrid = vol.getTimegrid(300.0, initial_timestep);
  auto flattimegrid = flatvol.getTimegrid(300.0, initial_timestep);
  ASSERT_EQ(flattimegrid.size(), timegrid.size());
  EXPECT_NEAR(flattimegrid[flattimegrid.size() - 1],
              timegrid[timegrid.size() - 1],
              initial_timestep * 0.0001);
}

// Returns sig_t_T
double forwardVol(
    double t0, double t, double T, double sig_0_t, double sig_0_T) {
  return std::sqrt((T * std::pow(sig_0_T, 2) - t * std::pow(sig_0_t, 2)) /
                   (T - t));
}

struct DermanExampleVol {
  double operator()(double t) const {
    if (t <= 1) return 0.2;
    if (t <= 2) return forwardVol(0, 1, 2, 0.2, 0.255);
    return forwardVol(0, 2, 3, 0.255, 0.311);
  }
};

TEST(VolatilityTest, Derman_VolSmile_13_6) {
  DermanExampleVol derman;
  Volatility vol(derman);
  const auto timegrid = vol.getTimegrid(3.0, 0.1);
  for (int i = 0; i < timegrid.size(); ++i) {
    std::cout << "i:" << i << "   t:" << timegrid[i] << std::endl;
  }
  EXPECT_TRUE(false);
}

}  // namespace
}  // namespace markets
