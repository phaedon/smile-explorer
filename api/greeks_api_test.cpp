#include "api/greeks_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <string_view>

#include "absl/status/status.h"
#include "derivatives/bsm.h"

namespace smileexplorer::api {
namespace {

VanillaPricingRequest atmCall(double dt_years = 1 / 500.) {
  VanillaPricingRequest request;
  request.tree = {.model = TreeModel::kCoxRossRubinstein,
                  .spot = 100,
                  .vol = {.vol = 0.2},
                  .rate = 0.05,
                  .dt_years = dt_years};
  request.option = {.strike = 100, .expiry_years = 1, .is_call = true};
  return request;
}

void expectInvalid(const absl::Status& status, std::string_view message) {
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code()) << status;
  EXPECT_THAT(status.message(), testing::HasSubstr(message));
}

TEST(GreeksApiTest, TreeGreeksMatchClosedForm) {
  for (bool is_call : {true, false}) {
    VanillaPricingRequest request = atmCall();
    request.option.is_call = is_call;
    const auto result = computeGreeks(request);
    ASSERT_TRUE(result.ok()) << result.status();
    const Greeks& tree = result->tree;
    const Greeks& exact = result->closed_form;
    EXPECT_NEAR(exact.delta, tree.delta, 0.005);
    EXPECT_NEAR(exact.gamma, tree.gamma, 0.02 * exact.gamma);
    EXPECT_NEAR(exact.theta, tree.theta, 0.02 * std::abs(exact.theta));
    EXPECT_NEAR(exact.vega, tree.vega, 0.01 * exact.vega);
    EXPECT_NEAR(exact.rho, tree.rho, 0.01 * std::abs(exact.rho));
  }
}

TEST(GreeksApiTest, SecondOrderGreeksAreTheClosedForms) {
  const auto result = computeGreeks(atmCall());
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_DOUBLE_EQ(vanna(100, 100, 0.2, 1, 0.05, 0), result->vanna);
  EXPECT_DOUBLE_EQ(volga(100, 100, 0.2, 1, 0.05, 0), result->volga);
  EXPECT_DOUBLE_EQ(call_charm(100, 100, 0.2, 1, 0.05, 0), result->charm);
  EXPECT_DOUBLE_EQ(colour(100, 100, 0.2, 1, 0.05, 0), result->colour);
}

// An in-the-money American put moves more with spot than its European
// counterpart: deep enough, it behaves like the stock it would be exercised
// against.
TEST(GreeksApiTest, AmericanPutDelta) {
  VanillaPricingRequest request = atmCall(1 / 200.);
  request.tree.spot = 90;
  request.option.is_call = false;
  const auto european = computeGreeks(request);
  request.option.is_american = true;
  const auto american = computeGreeks(request);
  ASSERT_TRUE(european.ok()) << european.status();
  ASSERT_TRUE(american.ok()) << american.status();
  EXPECT_LT(american->tree.delta, european->tree.delta);
  EXPECT_LT(american->tree.delta, 0);
  EXPECT_GT(american->tree.gamma, 0);
  EXPECT_TRUE(std::isnan(american->closed_form.delta));
  EXPECT_TRUE(std::isnan(american->vanna));
}

TEST(GreeksApiTest, ExplainsInvalidGreeksRequests) {
  VanillaPricingRequest request = atmCall();
  request.tree.model = TreeModel::kLocalVolatility;
  expectInvalid(computeGreeks(request).status(), "flat vol");

  request = atmCall(1.0);
  expectInvalid(computeGreeks(request).status(), "two steps");

  expectInvalid(computeGreeks(atmCall(), 0.3).status(), "bump");
  expectInvalid(computeGreeks(atmCall(), 0.01, 0).status(), "bump");

  request = atmCall();
  request.tree.spot = 0;
  expectInvalid(computeGreeks(request).status(), "spot");
}

TEST(GreeksApiTest, GreekSurface) {
  const GreekSurfaceRequest request{.greek = Greek::kGamma,
                                    .strike = 100,
                                    .vol = 0.2,
                                    .rate = 0.01,
                                    .is_call = true,
                                    .spots = {90, 100, 110},
                                    .expiries = {0.1, 1}};
  const auto values = greekSurface(request);
  ASSERT_TRUE(values.ok()) << values.status();
  ASSERT_EQ(6, std::ssize(*values));
  // Row by row: expiry 1, spot 110 is the last value.
  EXPECT_DOUBLE_EQ(gamma(110, 100, 0.2, 1, 0.01, 0), values->back());
  EXPECT_DOUBLE_EQ(gamma(90, 100, 0.2, 0.1, 0.01, 0), values->front());
  // Gamma at the money is higher closer to expiry.
  EXPECT_GT((*values)[1], (*values)[4]);

  // Every greek is available, with vega per unit of vol.
  GreekSurfaceRequest vega_request = request;
  vega_request.greek = Greek::kVega;
  const auto vegas = greekSurface(vega_request);
  ASSERT_TRUE(vegas.ok()) << vegas.status();
  EXPECT_DOUBLE_EQ(100 * vega(110, 100, 0.2, 1, 0.01, 0), vegas->back());
  for (Greek greek : {Greek::kPrice,
                      Greek::kDelta,
                      Greek::kTheta,
                      Greek::kVanna,
                      Greek::kVolga,
                      Greek::kCharm,
                      Greek::kColour}) {
    GreekSurfaceRequest r = request;
    r.greek = greek;
    r.is_call = false;
    const auto surface = greekSurface(r);
    ASSERT_TRUE(surface.ok()) << surface.status();
    for (double v : *surface) EXPECT_TRUE(std::isfinite(v));
  }
}

TEST(GreeksApiTest, ExplainsInvalidSurfaces) {
  const GreekSurfaceRequest valid{.spots = {100}, .expiries = {1}};
  GreekSurfaceRequest request = valid;
  request.greek = static_cast<Greek>(42);
  expectInvalid(greekSurface(request).status(), "unknown greek");
  request = valid;
  request.vol = 0;
  expectInvalid(greekSurface(request).status(), "vol");
  request = valid;
  request.spots = {};
  expectInvalid(greekSurface(request).status(), "between 1 and");
  request = valid;
  request.spots = {-1};
  expectInvalid(greekSurface(request).status(), "spots must be positive");
  request = valid;
  request.expiries = {0};
  expectInvalid(greekSurface(request).status(), "expiries must be positive");
}

DeltaHedgeRequest hedge(int rebalances, double realised_vol = 0.2) {
  return {.spot = 100,
          .strike = 100,
          .expiry_years = 0.25,
          .is_call = true,
          .implied_vol = 0.2,
          .realised_vol = realised_vol,
          .rate = 0.02,
          .rebalances = rebalances,
          .paths = 4000,
          .seed = 7};
}

TEST(GreeksApiTest, HedgingErrorShrinksWithRebalancing) {
  const auto coarse = simulateDeltaHedge(hedge(10));
  const auto fine = simulateDeltaHedge(hedge(160));
  ASSERT_TRUE(coarse.ok()) << coarse.status();
  ASSERT_TRUE(fine.ok()) << fine.status();
  // When vol is as expected, hedging breaks even on average...
  EXPECT_NEAR(0, coarse->mean, 0.05 * coarse->premium);
  EXPECT_NEAR(0, fine->mean, 0.05 * fine->premium);
  // ...and the error shrinks like 1 / sqrt(rebalances): 16x more often
  // should be about 4x tighter.
  EXPECT_NEAR(4, coarse->stddev / fine->stddev, 0.6);
}

TEST(GreeksApiTest, ShortOptionIsShortRealisedVol) {
  // Sold at 20% vol; spot realises 15%: the seller profits by about the
  // difference in option value, and loses about as much at 25%.
  for (double realised : {0.15, 0.25}) {
    const auto result = simulateDeltaHedge(hedge(200, realised));
    ASSERT_TRUE(result.ok()) << result.status();
    const double expected =
        std::exp(0.02 * 0.25) * (call(100, 100, 0.2, 0.25, 0.02, 0) -
                                 call(100, 100, realised, 0.25, 0.02, 0));
    EXPECT_NEAR(expected, result->mean, 0.15 * std::abs(expected));
  }
}

TEST(GreeksApiTest, HedgeSamplePath) {
  const auto result = simulateDeltaHedge(hedge(100));
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(101, std::ssize(result->times));
  EXPECT_DOUBLE_EQ(0.25, result->times.back());
  EXPECT_DOUBLE_EQ(result->pnl.front(), result->cumulative_pnl.back());
  EXPECT_TRUE(std::isnan(result->deltas.back()));
  // Theta earned and gamma paid account for most of the path's P&L.
  const double explained =
      result->cumulative_theta.back() + result->cumulative_gamma.back();
  EXPECT_NEAR(result->cumulative_pnl.back(), explained, 0.1 * result->premium);

  // The same seed gives the same paths.
  const auto again = simulateDeltaHedge(hedge(100));
  ASSERT_TRUE(again.ok());
  EXPECT_EQ(result->pnl, again->pnl);
}

TEST(GreeksApiTest, ExplainsInvalidHedges) {
  DeltaHedgeRequest request = hedge(10);
  request.spot = 0;
  expectInvalid(simulateDeltaHedge(request).status(), "spot");
  request = hedge(10);
  request.realised_vol = 0;
  expectInvalid(simulateDeltaHedge(request).status(), "realised_vol");
  request = hedge(10);
  request.rate = NAN;
  expectInvalid(simulateDeltaHedge(request).status(), "rate");
  request = hedge(2000);
  request.paths = 2000;
  expectInvalid(simulateDeltaHedge(request).status(), "product");
  request = hedge(0);
  expectInvalid(simulateDeltaHedge(request).status(), "rebalances");
}

}  // namespace
}  // namespace smileexplorer::api
