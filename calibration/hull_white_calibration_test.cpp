#include "calibration/hull_white_calibration.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <vector>

#include "absl/status/status.h"
#include "rates/zero_curve.h"

namespace smileexplorer {
namespace {

ZeroSpotCurve curve() {
  return ZeroSpotCurve({0.5, 1, 2, 5, 10}, {0.03, 0.032, 0.035, 0.04, 0.043});
}

// Co-terminal swaptions into swaps ending in 10 years.
std::vector<RateOptionQuote> coterminalSwaptions(
    const HullWhiteParams& params) {
  const ZeroSpotCurve c = curve();
  std::vector<RateOptionQuote> quotes;
  for (int expiry = 1; expiry <= 9; ++expiry) {
    RateOptionQuote q{.type = RateOptionType::kSwaption,
                      .expiry = static_cast<double>(expiry),
                      .tenor = 10.0 - expiry};
    q.normal_vol = hullWhiteQuote(c, params, q).normal_vol;
    quotes.push_back(q);
  }
  return quotes;
}

TEST(HullWhiteCalibrationTest, ModelQuoteIsConsistent) {
  // Short-dated, the normal vol of a caplet is close to sigma; longer-dated,
  // mean reversion pulls it down.
  const ZeroSpotCurve c = curve();
  const HullWhiteParams params{.mean_reversion = 0.1, .sigma = 0.01};
  const auto quote = [&](double expiry) {
    return hullWhiteQuote(c,
                          params,
                          {.type = RateOptionType::kCaplet,
                           .expiry = expiry,
                           .tenor = 0.25})
        .normal_vol;
  };
  EXPECT_NEAR(0.01, quote(0.25), 0.0005);
  EXPECT_LT(quote(5), quote(1));
}

TEST(HullWhiteCalibrationTest, RecoversKnownParameters) {
  const HullWhiteParams truth{.mean_reversion = 0.08, .sigma = 0.009};
  const auto quotes = coterminalSwaptions(truth);
  const auto start = std::chrono::steady_clock::now();
  const auto result = calibrateHullWhite(curve(), quotes);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_TRUE(result->converged);
  EXPECT_NEAR(truth.mean_reversion, result->params.mean_reversion, 1e-4);
  EXPECT_NEAR(truth.sigma, result->params.sigma, 1e-6);
  EXPECT_LT(result->rms_error, 1e-7);
  ASSERT_EQ(quotes.size(), result->model_vols.size());
  RecordProperty(
      "calibration_ms",
      static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
              .count()));
}

TEST(HullWhiteCalibrationTest, MixedCapletsAndSwaptions) {
  const HullWhiteParams truth{.mean_reversion = 0.2, .sigma = 0.012};
  const ZeroSpotCurve c = curve();
  std::vector<RateOptionQuote> quotes;
  for (double expiry : {0.5, 1.0, 2.0, 4.0}) {
    RateOptionQuote q{
        .type = RateOptionType::kCaplet, .expiry = expiry, .tenor = 0.25};
    q.normal_vol = hullWhiteQuote(c, truth, q).normal_vol;
    quotes.push_back(q);
  }
  RateOptionQuote swaption{
      .type = RateOptionType::kSwaption, .expiry = 2, .tenor = 5};
  swaption.normal_vol = hullWhiteQuote(c, truth, swaption).normal_vol;
  quotes.push_back(swaption);

  const auto result = calibrateHullWhite(c, quotes);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_NEAR(truth.mean_reversion, result->params.mean_reversion, 1e-3);
  EXPECT_NEAR(truth.sigma, result->params.sigma, 1e-5);
}

// Quotes the model can't match exactly: a humped vol curve. The fit is a
// compromise, with residuals of both signs.
TEST(HullWhiteCalibrationTest, CompromisesOnAHump) {
  auto quotes = coterminalSwaptions({.mean_reversion = 0.05, .sigma = 0.01});
  for (size_t i = 0; i < quotes.size(); ++i) {
    quotes[i].normal_vol += i >= 2 && i <= 5 ? 0.001 : -0.0005;
  }
  const auto result = calibrateHullWhite(curve(), quotes);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_GT(result->rms_error, 1e-4);
  int above = 0;
  int below = 0;
  for (size_t i = 0; i < quotes.size(); ++i) {
    (result->model_vols[i] > result->market_vols[i] ? above : below)++;
  }
  EXPECT_GT(above, 0);
  EXPECT_GT(below, 0);
}

void expectInvalid(const absl::Status& status, std::string_view message) {
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code()) << status;
  EXPECT_THAT(status.message(), testing::HasSubstr(message));
}

TEST(HullWhiteCalibrationTest, ExplainsInvalidQuotes) {
  const ZeroSpotCurve c = curve();
  const RateOptionQuote valid{.expiry = 1, .tenor = 2, .normal_vol = 0.01};
  expectInvalid(calibrateHullWhite(c, {valid}).status(), "between 2 and");
  RateOptionQuote bad = valid;
  bad.normal_vol = 0;
  expectInvalid(calibrateHullWhite(c, {valid, bad}).status(), "positive");
  bad = valid;
  bad.tenor = 2.5;
  expectInvalid(calibrateHullWhite(c, {valid, bad}).status(), "whole number");
  bad = valid;
  bad.type = static_cast<RateOptionType>(9);
  expectInvalid(calibrateHullWhite(c, {valid, bad}).status(), "unknown");
  expectInvalid(calibrateHullWhite(c, {valid, valid}, 0).status(),
                "fixed_payments_per_year");
  // A vol so high that sigma would have to exceed its bound.
  RateOptionQuote extreme = valid;
  extreme.normal_vol = 5;
  expectInvalid(calibrateHullWhite(c, {extreme, extreme}).status(),
                "no Hull-White parameters");
}

}  // namespace
}  // namespace smileexplorer
