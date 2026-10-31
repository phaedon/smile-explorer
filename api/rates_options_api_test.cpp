#include "api/rates_options_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "rates/zero_curve.h"

namespace smileexplorer::api {
namespace {

const RateCurveSpec kCurve{.maturities = {0.5, 1, 2, 5, 10},
                           .rates = {0.03, 0.032, 0.035, 0.04, 0.043}};

HullWhiteTreeSpec tree(double steps_per_year) {
  return {.mean_reversion = 0.1,
          .sigma = 0.01,
          .dt_years = 1 / steps_per_year,
          .curve = kCurve};
}

SwaptionRequest swaption2y5y(double steps_per_year = 24) {
  return {
      .tree = tree(steps_per_year),
      .swaption = {
          .expiry_years = 2, .tenor_years = 5, .fixed_payments_per_year = 1}};
}

void expectInvalid(const absl::Status& status, std::string_view message) {
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code()) << status;
  EXPECT_THAT(status.message(), testing::HasSubstr(message));
}

// The tree converges to Jamshidian's closed form: from well below with a few
// steps a year, to within a small fraction of a percent with many (as for
// options on zero-coupon bonds, the remaining gap closes slowly).
TEST(RatesOptionsApiTest, SwaptionTreeConvergesToJamshidian) {
  const auto coarse = priceSwaption(swaption2y5y(2));
  const auto fine = priceSwaption(swaption2y5y(48));
  ASSERT_TRUE(coarse.ok()) << coarse.status();
  ASSERT_TRUE(fine.ok()) << fine.status();
  const double analytic = fine->option.analytic_price;
  EXPECT_DOUBLE_EQ(analytic, coarse->option.analytic_price);
  EXPECT_GT(std::abs(coarse->option.price - analytic), 0.01 * analytic);
  EXPECT_NEAR(analytic, fine->option.price, 0.003 * analytic);
  EXPECT_NEAR(fine->analytic_normal_vol, fine->normal_vol, 2e-5);
}

TEST(RatesOptionsApiTest, SwaptionDetails) {
  const auto result = priceSwaption(swaption2y5y());
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(result->payment_times, testing::ElementsAre(3, 4, 5, 6, 7));
  EXPECT_DOUBLE_EQ(result->par_rate, result->fixed_rate);
  // The underlying bond is worth par at expiry on average, so today it is
  // worth the notional discounted from expiry.
  const ZeroSpotCurve curve(kCurve.maturities, kCurve.rates);
  EXPECT_NEAR(100 * curve.df(2), result->option.tree.market_bond_price, 1e-9);
  EXPECT_NEAR(result->option.tree.market_bond_price,
              result->option.tree.bond_price,
              1e-6);
  // The model's normal vol matches the calibration library's.
  const ModelQuote quote = hullWhiteQuote(
      curve,
      {.mean_reversion = 0.1, .sigma = 0.01},
      {.type = RateOptionType::kSwaption, .expiry = 2, .tenor = 5});
  EXPECT_NEAR(quote.normal_vol, result->analytic_normal_vol, 1e-10);
  EXPECT_NEAR(quote.price * 100, result->option.analytic_price, 1e-9);
}

TEST(RatesOptionsApiTest, PayerReceiverParityOnTheTree) {
  SwaptionRequest request = swaption2y5y();
  request.swaption.at_the_money = false;
  request.swaption.fixed_rate = 0.05;
  const auto payer = priceSwaption(request);
  request.swaption.is_payer = false;
  const auto receiver = priceSwaption(request);
  ASSERT_TRUE(payer.ok()) << payer.status();
  ASSERT_TRUE(receiver.ok()) << receiver.status();
  const ZeroSpotCurve curve(kCurve.maturities, kCurve.rates);
  const double swap = 100 * (curve.df(2) - curve.df(7) - 0.05 * payer->annuity);
  EXPECT_NEAR(swap, payer->option.price - receiver->option.price, 1e-6);
  EXPECT_NEAR(swap,
              payer->option.analytic_price - receiver->option.analytic_price,
              1e-9);
}

TEST(RatesOptionsApiTest, ExplainsInvalidSwaptions) {
  const auto with = [](auto modify) {
    SwaptionRequest request = swaption2y5y();
    modify(request);
    return priceSwaption(request).status();
  };
  using R = SwaptionRequest;
  expectInvalid(with([](R& r) { r.swaption.expiry_years = 0; }),
                "expiry_years");
  expectInvalid(with([](R& r) { r.swaption.notional = 0; }), "notional");
  expectInvalid(with([](R& r) { r.swaption.tenor_years = 2.5; }),
                "whole number");
  expectInvalid(with([](R& r) { r.swaption.fixed_payments_per_year = 0; }),
                "between 1 and 12");
  expectInvalid(with([](R& r) {
                  r.swaption.at_the_money = false;
                  r.swaption.fixed_rate = NAN;
                }),
                "fixed_rate");
  // Steps of 0.3 years don't land on the annual payment dates.
  expectInvalid(with([](R& r) { r.tree.dt_years = 0.3; }),
                "fall on one of the tree's steps");
  // Steps of 0.4 years land on 2.0 but not the expiry 2.2.
  expectInvalid(with([](R& r) {
                  r.tree.dt_years = 0.2;
                  r.swaption.expiry_years = 2.1;
                }),
                "fall on");
  expectInvalid(with([](R& r) { r.tree.curve.rates = {}; }), "curve");
}

CapRequest cap5y(double steps_per_year = 24) {
  return {.tree = tree(steps_per_year),
          .cap = {.maturity_years = 5, .payments_per_year = 4}};
}

TEST(RatesOptionsApiTest, CapletsMatchTheClosedForm) {
  const auto result = priceCap(cap5y());
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(19, std::ssize(result->prices));
  EXPECT_DOUBLE_EQ(0.25, result->fixing_times.front());
  EXPECT_DOUBLE_EQ(5.0, result->payment_times.back());
  for (size_t k = 0; k < result->prices.size(); ++k) {
    EXPECT_NEAR(result->analytic_prices[k],
                result->prices[k],
                0.03 * result->analytic_prices[k] + 1e-4)
        << k;
  }
  EXPECT_NEAR(
      result->analytic_price, result->price, 0.01 * result->analytic_price);
  // With mean reversion, caplet vols fall with expiry.
  EXPECT_GT(result->normal_vols.front(), result->normal_vols.back());
}

TEST(RatesOptionsApiTest, CapFloorParity) {
  CapRequest request = cap5y();
  request.cap.at_the_money = false;
  request.cap.strike = 0.045;
  const auto cap = priceCap(request);
  request.cap.is_cap = false;
  const auto floor = priceCap(request);
  ASSERT_TRUE(cap.ok()) << cap.status();
  ASSERT_TRUE(floor.ok()) << floor.status();
  const ZeroSpotCurve curve(kCurve.maturities, kCurve.rates);
  double fras = 0;
  for (size_t k = 0; k < cap->fixing_times.size(); ++k) {
    fras += 100 * (curve.df(cap->fixing_times[k]) -
                   (1 + 0.045 * 0.25) * curve.df(cap->payment_times[k]));
  }
  EXPECT_NEAR(fras, cap->analytic_price - floor->analytic_price, 1e-9);
  EXPECT_NEAR(fras, cap->price - floor->price, 1e-4);
}

TEST(RatesOptionsApiTest, ExplainsInvalidCaps) {
  const auto with = [](auto modify) {
    CapRequest request = cap5y();
    modify(request);
    return priceCap(request).status();
  };
  using R = CapRequest;
  expectInvalid(with([](R& r) { r.cap.notional = -1; }), "notional");
  expectInvalid(with([](R& r) { r.cap.maturity_years = 0.25; }),
                "at least two periods");
  expectInvalid(with([](R& r) { r.cap.maturity_years = 1.1; }), "whole number");
  expectInvalid(with([](R& r) {
                  r.cap.at_the_money = false;
                  r.cap.strike = NAN;
                }),
                "strike");
  expectInvalid(with([](R& r) { r.tree.dt_years = 0.1; }),
                "don't fall on the tree's steps");
  expectInvalid(with([](R& r) { r.tree.sigma = 0; }), "sigma");
}

std::vector<RateOptionQuote> coterminal() {
  std::vector<RateOptionQuote> quotes;
  for (int expiry = 1; expiry <= 4; ++expiry) {
    quotes.push_back({.type = RateOptionType::kSwaption,
                      .expiry = static_cast<double>(expiry),
                      .tenor = 5.0 - expiry});
  }
  return quotes;
}

TEST(RatesOptionsApiTest, CalibrationRoundTrip) {
  ModelVolsRequest model{.curve = kCurve,
                         .params = {.mean_reversion = 0.15, .sigma = 0.011},
                         .quotes = coterminal()};
  const auto vols = modelNormalVols(model);
  ASSERT_TRUE(vols.ok()) << vols.status();
  CalibrationRequest request{.curve = kCurve, .quotes = coterminal()};
  for (size_t i = 0; i < vols->size(); ++i) {
    request.quotes[i].normal_vol = (*vols)[i];
  }
  const auto result = calibrateHullWhite(request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_NEAR(0.15, result->params.mean_reversion, 1e-4);
  EXPECT_NEAR(0.011, result->params.sigma, 1e-6);
}

TEST(RatesOptionsApiTest, ExplainsInvalidCalibrations) {
  CalibrationRequest request{.curve = {}, .quotes = coterminal()};
  expectInvalid(calibrateHullWhite(request).status(), "curve");
  ModelVolsRequest model{.curve = kCurve,
                         .params = {.mean_reversion = 0, .sigma = 0.01},
                         .quotes = coterminal()};
  expectInvalid(modelNormalVols(model).status(), "mean_reversion");
  model.params.mean_reversion = 0.1;
  model.quotes[0].tenor = 0;
  expectInvalid(modelNormalVols(model).status(), "tenor");
  model.curve = {};
  expectInvalid(modelNormalVols(model).status(), "curve");
  model.curve = kCurve;
  model.quotes.assign(401, coterminal()[0]);
  expectInvalid(modelNormalVols(model).status(), "at most");
}

}  // namespace
}  // namespace smileexplorer::api
