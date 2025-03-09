#include "volatility/volatility.h"

#include <gtest/gtest.h>

namespace smileexplorer {

namespace {

TEST(VolatilityTest, FlatVol) {
  Volatility vol(FlatVol(0.15));
  EXPECT_DOUBLE_EQ(0.15, vol.get());
}

struct TermStrucVol {
  double operator()(double t) const { return 1.1 * t; }
};

TEST(VolatilityTest, TermStrucVol) {
  TermStrucVol tsm;
  Volatility vol(tsm);
  EXPECT_DOUBLE_EQ(0.11, vol.get(0.1));
}

struct VolSurfaceFn {
  double operator()(double t, double s) const { return t + s; }
};

TEST(VolatilityTest, VolSurface) {
  VolSurfaceFn volfn;
  Volatility vol(volfn);

  EXPECT_DOUBLE_EQ(3, vol.get(1, 2));
}

TEST(VolatilityTest, ConstantTimeGrid) {
  Volatility vol(FlatVol(0.15));
  auto timegrid = vol.generateTimegrid(3.0, 0.3);
  ASSERT_EQ(11, timegrid.size());
  // The maturity is an exact multiple of the timestep.
  EXPECT_DOUBLE_EQ(3.0, timegrid.time(timegrid.size() - 1));

  // If the maturity is not an exact multiple, ensure that
  // the timegrid extends past the end of the desired maturity.
  timegrid = vol.generateTimegrid(3.0, 0.4);
  ASSERT_EQ(9, timegrid.size());
  EXPECT_DOUBLE_EQ(3.2, timegrid.time(timegrid.size() - 1));
}

struct FlatTermStrucVol {
  static constexpr VolSurfaceFnType type = VolSurfaceFnType::kTermStructure;
  double operator()(double t) const { return 0.15; }
};

TEST(VolatilityTest, TimeVaryingGridMatchesFlatVol) {
  // We use FlatTermStrucVol to ensure that the time-varying
  // implementation matches that of the FlatVol in the simplest case.
  FlatTermStrucVol tsm;
  Volatility vol(tsm);
  Volatility flatvol(FlatVol(0.15));

  const double initial_timestep = 0.3;
  auto timegrid = vol.generateTimegrid(300.0, initial_timestep);
  auto flattimegrid = flatvol.generateTimegrid(300.0, initial_timestep);
  ASSERT_EQ(flattimegrid.size(), timegrid.size());
  EXPECT_NEAR(flattimegrid.time(flattimegrid.size() - 1),
              timegrid.time(timegrid.size() - 1),
              initial_timestep * 0.0001);
}

struct DermanExampleVol {
  static constexpr VolSurfaceFnType type = VolSurfaceFnType::kTermStructure;
  double operator()(double t) const {
    if (t <= 1) return 0.2;
    if (t <= 2) return forwardVol(0, 1, 2, 0.2, 0.255);
    return forwardVol(0, 2, 3, 0.255, 0.311);
  }
};

TEST(VolatilityTest, Derman_VolSmile_13_6) {
  DermanExampleVol derman;
  Volatility vol(derman);

  // Verify the forward vols on page 466.
  EXPECT_NEAR(0.3, vol.get(1.5), 0.001);
  EXPECT_NEAR(0.4, vol.get(2.5), 0.001);

  const auto timegrid = vol.generateTimegrid(3.0, 0.1);

  // Verify the dts in years 2 and 3:
  EXPECT_NEAR(0.044, timegrid.dt(30), 0.001);
  EXPECT_NEAR(0.025, timegrid.dt(50), 0.001);

  for (int i = 1; i < timegrid.size(); ++i) {
    if (timegrid.time(i) >= 1 && timegrid.time(i - 1) < 1) {
      EXPECT_EQ(10, i - 1);
    }

    if (timegrid.time(i) >= 2 && timegrid.time(i - 1) < 2) {
      EXPECT_EQ(23, i - 10 + 1);
    }

    if (timegrid.time(i) >= 3 && timegrid.time(i - 1) < 3) {
      // TODO: I'm not sure why the correction of +2 is needed here, but likely
      // it's to avoid double-counting the timesteps at each year-boundary.
      // Also, as Derman notes there is rounding error since we are not
      // guaranteed an integer number of steps.
      EXPECT_EQ(40, i - 23 - 10 + 2);
    }
  }

  // This result suggests that the total number of timesteps matches Derman's
  // estimate.
  EXPECT_EQ(10 + 23 + 40 - 1, timegrid.size());
}

}  // namespace
}  // namespace smileexplorer
