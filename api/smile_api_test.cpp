#include "api/smile_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "api/pricing_api.h"

namespace smileexplorer::api {
namespace {

const std::vector<double> kExpiries{0.25, 0.5, 1.0, 2.0};
const std::vector<double> kStrikes{
    70, 75, 80, 85, 90, 95, 100, 105, 110, 115, 120, 125, 130};

// An index-like skew, quadratic in standardised moneyness (not SSVI).
SmileQuotes skewQuotes() {
  SmileQuotes q{
      .spot = 100, .rate = 0.02, .expiries = kExpiries, .strikes = kStrikes};
  const std::vector<double> atm{0.22, 0.20, 0.19, 0.185};
  for (size_t i = 0; i < kExpiries.size(); ++i) {
    const double t = kExpiries[i];
    for (double k : kStrikes) {
      const double m = std::log(k / (100 * std::exp(0.02 * t))) / std::sqrt(t);
      q.vols.push_back(atm[i] - 0.06 * m + 0.02 * m * m);
    }
  }
  return q;
}

SmileGridRequest gridFor(const SmileFit& fit) {
  return {.spot = 100,
          .rate = 0.02,
          .ssvi = {.rho = fit.rho,
                   .eta = fit.eta,
                   .gamma = fit.gamma,
                   .expiries = kExpiries,
                   .thetas = fit.thetas},
          .strikes = kStrikes,
          .times = kExpiries};
}

// Quotes -> SSVI -> Dupire local vol -> tree -> implied vols: the tree
// reprices the fitted surface, more closely the more steps it has.
TEST(SmileApiTest, TreeRepricesTheCalibratedSurface) {
  const auto fit = calibrateSmile(skewQuotes());
  ASSERT_TRUE(fit.ok()) << fit.status();
  EXPECT_LT(fit->rms_error, 0.002);
  EXPECT_TRUE(fit->butterfly_ok);

  double previous = INFINITY;
  for (int steps : {20, 40, 100, 200}) {
    const auto r =
        repriceSmile({.grid = gridFor(*fit), .steps_per_year = steps});
    ASSERT_TRUE(r.ok()) << r.status();
    EXPECT_LT(r->rms_error, previous) << steps;
    previous = r->rms_error;
  }
  const auto fine =
      repriceSmile({.grid = gridFor(*fit), .steps_per_year = 200});
  ASSERT_TRUE(fine.ok());
  EXPECT_LT(fine->rms_error, 0.001);
  for (size_t n = 0; n < fine->residuals.size(); ++n) {
    EXPECT_LT(std::abs(fine->residuals[n]), 0.004) << n;
  }
}

TEST(SmileApiTest, SurfacesMatchTheLibrary) {
  const auto fit = calibrateSmile(skewQuotes());
  ASSERT_TRUE(fit.ok());
  const SmileGridRequest grid = gridFor(*fit);
  const auto surfaces = smileSurfaces(grid);
  ASSERT_TRUE(surfaces.ok()) << surfaces.status();
  ASSERT_EQ(kStrikes.size() * kExpiries.size(), surfaces->implied_vols.size());
  // At the quotes, the surface's implied vols are the fit's.
  for (size_t n = 0; n < surfaces->implied_vols.size(); ++n) {
    EXPECT_NEAR(fit->fitted_vols[n], surfaces->implied_vols[n], 1e-12);
  }
  // Local vol is steeper than implied vol across strikes (rule of two).
  const size_t row = 2 * kStrikes.size();  // 1 year
  const double implied_slope =
      surfaces->implied_vols[row + 7] - surfaces->implied_vols[row + 5];
  const double local_slope =
      surfaces->local_vols[row + 7] - surfaces->local_vols[row + 5];
  EXPECT_LT(local_slope, implied_slope);
  EXPECT_LT(implied_slope, 0);
  EXPECT_EQ(0, surfaces->clamped);
}

TEST(SmileApiTest, ImpliedDensity) {
  const auto fit = calibrateSmile(skewQuotes());
  ASSERT_TRUE(fit.ok());
  SmileGridRequest grid = gridFor(*fit);
  grid.times = {1.0};
  grid.strikes.clear();
  for (double k = 1; k < 400; k += 2) grid.strikes.push_back(k);
  const auto density = impliedDensity(grid);
  ASSERT_TRUE(density.ok()) << density.status();
  double total = 0;
  for (double p : *density) {
    EXPECT_GE(p, 0);
    total += p * 2;
  }
  EXPECT_NEAR(1.0, total, 2e-3);
}

// The fitted surface prices on the local-vol tree through priceVanilla too,
// against Black-Scholes at the surface's own implied vol for the strike.
TEST(SmileApiTest, PriceVanillaOnTheCalibratedTree) {
  const auto fit = calibrateSmile(skewQuotes());
  ASSERT_TRUE(fit.ok());
  std::vector<double> atm_vols;
  for (size_t i = 0; i < kExpiries.size(); ++i) {
    atm_vols.push_back(std::sqrt(fit->thetas[i] / kExpiries[i]));
  }
  VanillaPricingRequest request;
  request.tree = {.model = TreeModel::kLocalVolatility,
                  .spot = 100,
                  .vol = {.model = VolModel::kSsviLocalVol,
                          .vol = 0.2,
                          .term_maturities = kExpiries,
                          .term_vols = atm_vols,
                          .ssvi_rho = fit->rho,
                          .ssvi_eta = fit->eta,
                          .ssvi_gamma = fit->gamma},
                  .rate = 0.02,
                  .dt_years = 1 / 200.};
  request.option = {.strike = 90, .expiry_years = 1, .is_call = false};
  const auto result = priceVanilla(request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_NEAR(result->bsm_price, result->price, 0.02);
  EXPECT_NEAR(
      fit->fitted_vols[2 * kStrikes.size() + 4], result->bsm_vol, 1e-12);

  // Only the local volatility tree takes it.
  request.tree.model = TreeModel::kCoxRossRubinstein;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceVanilla(request).status().code());
  request.tree.model = TreeModel::kLocalVolatility;
  request.tree.vol.ssvi_gamma = 1.5;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceVanilla(request).status().code());
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            evaluateVol(request.tree.vol, 100, {100}).status().code());
}

void expectInvalid(const absl::Status& status, std::string_view message) {
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code()) << status;
  EXPECT_THAT(status.message(), testing::HasSubstr(message));
}

TEST(SmileApiTest, ExplainsInvalidRequests) {
  const auto fit = calibrateSmile(skewQuotes());
  ASSERT_TRUE(fit.ok());
  const SmileGridRequest valid = gridFor(*fit);
  const auto with = [&](auto modify) {
    SmileGridRequest r = valid;
    modify(r);
    return smileSurfaces(r).status();
  };
  using R = SmileGridRequest;
  expectInvalid(with([](R& r) { r.spot = 0; }), "spot");
  expectInvalid(with([](R& r) { r.ssvi.rho = 1; }), "rho");
  expectInvalid(with([](R& r) { r.ssvi.thetas.pop_back(); }), "matching");
  expectInvalid(with([](R& r) { r.strikes = {}; }), "between 1 and");
  expectInvalid(with([](R& r) { r.strikes = {-1}; }),
                "strikes must be positive");
  expectInvalid(with([](R& r) { r.times = {0}; }), "times must be positive");
  expectInvalid(impliedDensity({.spot = 0}).status(), "spot");

  expectInvalid(repriceSmile({.grid = valid, .steps_per_year = 1}).status(),
                "steps_per_year");
  expectInvalid(repriceSmile({.grid = valid, .steps_per_year = 25}).status(),
                "doesn't fall on");
  SmileGridRequest far = valid;
  far.times = {20};
  expectInvalid(repriceSmile({.grid = far, .steps_per_year = 100}).status(),
                "more than");
  SmileGridRequest bad = valid;
  bad.spot = -1;
  expectInvalid(repriceSmile({.grid = bad}).status(), "spot");
  expectInvalid(calibrateSmile({}).status(), "expiries");
}

}  // namespace
}  // namespace smileexplorer::api
