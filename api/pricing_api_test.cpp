#include "api/pricing_api.h"

#include <gtest/gtest.h>

#include <cmath>

#include "absl/status/status.h"
#include "derivatives/derivative.h"
#include "derivatives/vanilla_option.h"
#include "rates/zero_curve.h"
#include "trees/propagators.h"
#include "trees/stochastic_tree_model.h"
#include "volatility/volatility.h"

namespace smileexplorer::api {
namespace {

VanillaPricingRequest exampleRequest() {
  VanillaPricingRequest request;
  request.tree = {.model = TreeModel::kCoxRossRubinstein,
                  .spot = 100,
                  .vol = 0.158745,
                  .rate = 0.12,
                  .dt_years = 1 / 52.};
  request.option = {.strike = 105, .expiry_years = 1.0, .is_call = true};
  return request;
}

TEST(PricingApiTest, MatchesLibraryPrice) {
  const VanillaPricingRequest request = exampleRequest();
  const auto result = priceVanilla(request);
  ASSERT_TRUE(result.ok()) << result.status();

  // The same computation, composed directly from the library.
  StochasticTreeModel<CRRPropagator> asset(BinomialTree(1.0, 1 / 52.),
                                           CRRPropagator(100));
  asset.forwardPropagate(Volatility(FlatVol(0.158745)));
  ZeroSpotCurve curve({1.0, 10.0}, {0.12, 0.12});
  SingleAssetDerivative deriv(&asset.binomialTree(), &curve);
  EXPECT_DOUBLE_EQ(deriv.price(VanillaOption(105, OptionPayoff::Call), 1.0),
                   result->price);
  EXPECT_DOUBLE_EQ(result->price, result->option_values[0]);
}

// Mirrors DerivativeTest.TreePricingApproxEqualsBSM.
TEST(PricingApiTest, ConvergesToBlackScholes) {
  VanillaPricingRequest request = exampleRequest();
  request.tree.dt_years = 1 / 360.;
  const auto result = priceVanilla(request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_NEAR(result->bsm_price, result->price, 0.005);

  request.tree.model = TreeModel::kJarrowRudd;
  const auto jr_result = priceVanilla(request);
  ASSERT_TRUE(jr_result.ok()) << jr_result.status();
  EXPECT_NEAR(result->price, jr_result->price, 0.005);
}

TEST(PricingApiTest, ArraySizes) {
  const auto result = priceVanilla(exampleRequest());
  ASSERT_TRUE(result.ok()) << result.status();
  const size_t expiry = result->expiry_index;
  EXPECT_EQ(52, result->expiry_index);
  EXPECT_NEAR(1.0, result->times[expiry], 1e-12);
  EXPECT_EQ(expiry + 1, result->times.size());
  EXPECT_EQ(expiry + 1, result->forward_dfs.size());
  const size_t num_nodes = (expiry + 1) * (expiry + 2) / 2;
  EXPECT_EQ(num_nodes, result->asset_values.size());
  EXPECT_EQ(num_nodes, result->option_values.size());
  EXPECT_EQ(num_nodes, result->exercise_values.size());
  EXPECT_EQ(num_nodes, result->continuation_values.size());
  EXPECT_EQ(num_nodes, result->up_probs.size());
  EXPECT_EQ(num_nodes, result->arrow_debreu.size());
  EXPECT_EQ(100, result->asset_values[nodeIndex(0, 0)]);
}

// The per-node values must reproduce the backward induction exactly.
void expectConsistentInduction(const VanillaPricingResult& r,
                               bool is_american) {
  const int expiry = r.expiry_index;
  for (int i = 0; i <= expiry; ++i) {
    const int n = nodeIndex(expiry, i);
    EXPECT_DOUBLE_EQ(r.exercise_values[n], r.option_values[n]);
    EXPECT_TRUE(std::isnan(r.continuation_values[n]));
  }
  for (int t = 0; t < expiry; ++t) {
    for (int i = 0; i <= t; ++i) {
      const int n = nodeIndex(t, i);
      const double expected =
          is_american ? std::max(r.exercise_values[n], r.continuation_values[n])
                      : r.continuation_values[n];
      EXPECT_NEAR(expected, r.option_values[n], 1e-12)
          << "at node (" << t << ", " << i << ")";
    }
  }
}

TEST(PricingApiTest, NodeValuesExplainBackwardInduction) {
  VanillaPricingRequest request = exampleRequest();
  request.option = {.strike = 110, .expiry_years = 1.0, .is_call = false};
  const auto european = priceVanilla(request);
  ASSERT_TRUE(european.ok()) << european.status();
  expectConsistentInduction(*european, /*is_american=*/false);

  request.option.is_american = true;
  const auto american = priceVanilla(request);
  ASSERT_TRUE(american.ok()) << american.status();
  expectConsistentInduction(*american, /*is_american=*/true);

  EXPECT_GT(american->price, european->price);
  // The European closed form is reported for both.
  EXPECT_DOUBLE_EQ(european->bsm_price, american->bsm_price);
}

TEST(PricingApiTest, ArrowDebreuPrices) {
  const auto result = priceVanilla(exampleRequest());
  ASSERT_TRUE(result.ok()) << result.status();
  const auto& r = *result;

  // At each time, the Arrow-Debreu prices sum to the discount factor.
  for (int t = 0; t <= r.expiry_index; ++t) {
    double sum = 0;
    for (int i = 0; i <= t; ++i) sum += r.arrow_debreu[nodeIndex(t, i)];
    EXPECT_NEAR(std::exp(-0.12 * r.times[t]), sum, 1e-9) << "at t=" << t;
  }

  // A European option is worth its payoff weighted by Arrow-Debreu prices.
  double price = 0;
  for (int i = 0; i <= r.expiry_index; ++i) {
    const int n = nodeIndex(r.expiry_index, i);
    price += r.arrow_debreu[n] * r.exercise_values[n];
  }
  EXPECT_NEAR(r.price, price, 1e-9);
}

TEST(PricingApiTest, RejectsInvalidRequests) {
  VanillaPricingRequest request = exampleRequest();
  request.tree.vol = -0.1;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceVanilla(request).status().code());

  request = exampleRequest();
  request.option.expiry_years = 0;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceVanilla(request).status().code());

  request = exampleRequest();
  request.tree.dt_years = 1 / 10000.;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceVanilla(request).status().code());

  // With a large timestep and high rate relative to vol, the forward lies
  // outside the up/down range, so there is no risk-neutral measure.
  request = exampleRequest();
  request.tree.vol = 0.01;
  request.tree.rate = 0.5;
  request.tree.dt_years = 0.5;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceVanilla(request).status().code());
}

}  // namespace
}  // namespace smileexplorer::api
