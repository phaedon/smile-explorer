#include "volatility/dupire_local_vol.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "volatility/volatility.h"

namespace smileexplorer {
namespace {

std::vector<double> totalVariances(const std::vector<double>& t,
                                   const std::vector<double>& vols) {
  std::vector<double> out;
  for (size_t i = 0; i < t.size(); ++i) out.push_back(vols[i] * vols[i] * t[i]);
  return out;
}

TEST(DupireLocalVolTest, FlatSurfaceGivesFlatLocalVol) {
  // No smile (tiny eta) and a flat term structure.
  const DupireLocalVol local(
      SsviSurface(0, 1e-9, 0.5, {1}, totalVariances({1}, {0.2})), 100, 0.03);
  for (double t : {0.01, 0.5, 1.0, 2.0}) {
    for (double s : {60.0, 100.0, 150.0}) {
      EXPECT_NEAR(0.2, local(s, t), 1e-6) << s << " " << t;
    }
  }
}

// With no smile, local vol is the forward vol between expiries, exactly as
// in PiecewiseTermStructureVol.
TEST(DupireLocalVolTest, TermStructureGivesForwardVols) {
  const std::vector<double> t{1, 2, 3};
  const std::vector<double> vols{0.2, 0.255, 0.311};
  const DupireLocalVol local(
      SsviSurface(0, 1e-9, 0.5, t, totalVariances(t, vols)), 100, 0.0);
  const PiecewiseTermStructureVol term_structure(t, vols);
  for (double time : {0.5, 1.5, 2.5}) {
    EXPECT_NEAR(term_structure(time), local(100, time), 1e-6) << time;
    EXPECT_NEAR(term_structure(time), local(80, time), 1e-6) << time;
  }
}

// Derman's rule of two: near the money, local vol changes with the asset
// price about twice as fast as implied vol changes with strike. The rule
// assumes a smile that doesn't change with expiry; SSVI's flattens (here like
// 1 / sqrt(T)), so local vol's skew is steeper earlier in the option's life
// and the ratio depends on when it is read. At expiry it is a little under
// two, whatever the expiry.
TEST(DupireLocalVolTest, LocalSkewIsAboutTwiceTheImpliedSkew) {
  for (double expiry : {0.05, 0.25}) {
    const std::vector<double> t{expiry, 1.0};
    const SsviSurface surface(-0.6, 0.8, 0.5, t, totalVariances(t, {0.2, 0.2}));
    const DupireLocalVol local(surface, 100, 0.0);
    const double implied_slope =
        (surface.impliedVol(std::log(100.1 / 100), expiry) -
         surface.impliedVol(std::log(99.9 / 100), expiry)) /
        0.2;
    const double local_slope =
        (local(100.1, expiry) - local(99.9, expiry)) / 0.2;
    ASSERT_LT(implied_slope, 0);
    EXPECT_NEAR(1.75, local_slope / implied_slope, 0.05) << expiry;
    // Earlier in the option's life, local vol's skew is steeper still.
    const double early_slope =
        (local(100.1, expiry / 2) - local(99.9, expiry / 2)) / 0.2;
    EXPECT_LT(early_slope, local_slope);
  }
}

TEST(DupireLocalVolTest, ClampsWhereTheFormulaBreaksDown) {
  // So much curvature that the surface admits butterfly arbitrage: the
  // formula's denominator turns negative somewhere in the wings.
  const std::vector<double> t{0.25, 0.5};
  const SsviSurface bad(-0.9, 20, 0.4, t, totalVariances(t, {0.2, 0.2}));
  ASSERT_FALSE(bad.butterflyOk());
  const DupireLocalVol local(bad, 100, 0.0);
  bool clamped = false;
  for (double s = 50; s < 150; s += 1) {
    const double vol = local(s, 0.2);
    EXPECT_GE(vol, DupireLocalVol::kMinVol);
    EXPECT_LE(vol, DupireLocalVol::kMaxVol);
    clamped = clamped || local.isClamped(s, 0.2);
  }
  EXPECT_TRUE(clamped);
  // Near the money, a good surface is not clamped.
  const DupireLocalVol good(
      SsviSurface(-0.6, 0.8, 0.5, t, totalVariances(t, {0.2, 0.2})), 100, 0.0);
  EXPECT_FALSE(good.isClamped(100, 0.2));
}

TEST(DupireLocalVolTest, UniformTimegrid) {
  const Volatility vol(DupireLocalVol(
      SsviSurface(0, 1e-9, 0.5, {1}, totalVariances({1}, {0.2})), 100, 0.0));
  const Timegrid grid = vol.generateTimegrid(1.0, 0.25);
  ASSERT_EQ(5, grid.size());
  EXPECT_DOUBLE_EQ(0.75, grid.time(3));
}

}  // namespace
}  // namespace smileexplorer
