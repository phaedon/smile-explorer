#include "api/pricing_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <string_view>
#include <vector>

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
                  .vol = {.vol = 0.158745},
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
  request.tree.vol.vol = -0.1;
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
  request.tree.vol.vol = 0.01;
  request.tree.rate = 0.5;
  request.tree.dt_years = 0.5;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceVanilla(request).status().code());
}

VolSpec dermanTermStructure() {
  // Derman, The Volatility Smile, 13.6.
  return {.model = VolModel::kTermStructure,
          .term_maturities = {1, 2, 3},
          .term_vols = {0.2, 0.255, 0.311}};
}

TEST(PricingApiTest, TermStructureMatchesBlackScholesAtImpliedVol) {
  VanillaPricingRequest request = exampleRequest();
  request.tree.vol = dermanTermStructure();
  request.tree.rate = 0.05;
  request.tree.dt_years = 0.01;
  request.option = {.strike = 100, .expiry_years = 3.0, .is_call = true};
  const auto result = priceVanilla(request);
  ASSERT_TRUE(result.ok()) << result.status();

  EXPECT_NEAR(0.311, result->bsm_vol, 1e-3);
  EXPECT_NEAR(result->bsm_price, result->price, 0.05);

  // Timesteps shrink as the forward vol rises in years 2 and 3.
  const auto& times = result->times;
  EXPECT_GT(times[1] - times[0], times.back() - times[times.size() - 2]);
}

TEST(PricingApiTest, LocalVolTreeWithFlatVolMatchesBlackScholes) {
  VanillaPricingRequest request = exampleRequest();
  request.tree.model = TreeModel::kLocalVolatility;
  request.tree.vol = {.vol = 0.2};
  request.tree.rate = 0.05;
  request.tree.dt_years = 1 / 200.;
  request.option = {.strike = 100, .expiry_years = 1.0, .is_call = true};
  const auto result = priceVanilla(request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_NEAR(result->bsm_price, result->price, 0.02);
}

// Prices European calls across strikes on a local-vol tree with a skew, and
// backs out their Black-Scholes implied vols.
TEST(PricingApiTest, LocalVolSkewProducesImpliedVolSkew) {
  VanillaPricingRequest request = exampleRequest();
  request.tree.model = TreeModel::kLocalVolatility;
  request.tree.vol = {.model = VolModel::kSigmoidSmile,
                      .vol = 0.15,
                      .smile_range = 0.2,
                      .smile_stretch = 0.05};
  request.tree.rate = 0.05;
  request.tree.dt_years = 1 / 100.;

  std::vector<double> implied_vols;
  for (double strike : {80.0, 100.0, 120.0}) {
    request.option = {.strike = strike, .expiry_years = 1.0, .is_call = true};
    const auto result = priceVanilla(request);
    ASSERT_TRUE(result.ok()) << result.status();
    const auto implied = impliedVolatility({.price = result->price,
                                            .spot = 100,
                                            .strike = strike,
                                            .expiry_years = 1.0,
                                            .rate = 0.05,
                                            .is_call = true});
    ASSERT_TRUE(implied.ok()) << implied.status();
    implied_vols.push_back(*implied);
  }
  EXPECT_GT(implied_vols[0], implied_vols[1]);
  EXPECT_GT(implied_vols[1], implied_vols[2]);
  // The ATM implied vol is close to the local vol at spot (0.25).
  EXPECT_NEAR(0.25, implied_vols[1], 0.02);
}

TEST(PricingApiTest, ImpliedVolatilityInvertsBlackScholes) {
  for (bool is_call : {true, false}) {
    const VanillaOption option(
        90, is_call ? OptionPayoff::Call : OptionPayoff::Put);
    const double price = option.blackScholes(100, 0.23, 0.5, 0.03, 0.0);
    const auto vol = impliedVolatility({.price = price,
                                        .spot = 100,
                                        .strike = 90,
                                        .expiry_years = 0.5,
                                        .rate = 0.03,
                                        .is_call = is_call});
    ASSERT_TRUE(vol.ok()) << vol.status();
    EXPECT_NEAR(0.23, *vol, 1e-9);
  }

  // A call can't be worth more than the stock.
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            impliedVolatility({.price = 101, .spot = 100, .strike = 90})
                .status()
                .code());
}

TEST(PricingApiTest, EvaluateVol) {
  const auto term = evaluateVol(dermanTermStructure(), 100, {0.5, 1.5, 2.5});
  ASSERT_TRUE(term.ok()) << term.status();
  EXPECT_NEAR(0.2, (*term)[0], 1e-12);
  EXPECT_NEAR(0.3, (*term)[1], 1e-3);  // Derman, p. 466.
  EXPECT_NEAR(0.4, (*term)[2], 1e-3);

  const auto skew = evaluateVol({.model = VolModel::kSigmoidSmile,
                                 .vol = 0.15,
                                 .smile_range = 0.2,
                                 .smile_stretch = 0.1},
                                100,
                                {100});
  ASSERT_TRUE(skew.ok()) << skew.status();
  EXPECT_NEAR(0.25, (*skew)[0], 1e-12);
}

TEST(PricingApiTest, RejectsInvalidVolSpecs) {
  VanillaPricingRequest request = exampleRequest();

  // Total variance must not fall: 2 * 0.1^2 < 1 * 0.2^2.
  request.tree.vol = {.model = VolModel::kTermStructure,
                      .term_maturities = {1, 2},
                      .term_vols = {0.2, 0.1}};
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceVanilla(request).status().code());

  // A smile needs the local vol tree, and vice versa for a term structure.
  request.tree.vol = {.model = VolModel::kSigmoidSmile, .vol = 0.2};
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceVanilla(request).status().code());
  request.tree.model = TreeModel::kLocalVolatility;
  request.tree.vol = dermanTermStructure();
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceVanilla(request).status().code());
}

// Hull, "Options, Futures...", Figure 13.12 (as in
// DerivativeTest.CurrencyOptionTreePricing): a 3-month call on an exchange
// rate, on a tree with monthly steps.
FxVanillaPricingRequest hullFxRequest() {
  FxVanillaPricingRequest request;
  request.tree = {.spot = 0.61,
                  .vol = 0.12,
                  .domestic = {.maturities = {1}, .rates = {0.05}},
                  .foreign = {.maturities = {1}, .rates = {0.07}},
                  .dt_years = 1 / 12.};
  request.option = {.strike = 0.6, .expiry_years = 0.25, .is_call = true};
  return request;
}

TEST(PricingApiTest, FxMatchesHull) {
  FxVanillaPricingRequest request = hullFxRequest();
  const auto european = priceFxVanilla(request);
  ASSERT_TRUE(european.ok()) << european.status();
  EXPECT_NEAR(0.01860, european->price, 0.00001);

  request.option.is_american = true;
  const auto american = priceFxVanilla(request);
  ASSERT_TRUE(american.ok()) << american.status();
  EXPECT_NEAR(0.01888, american->price, 0.00001);
}

TEST(PricingApiTest, FxConvergesToGarmanKohlhagen) {
  FxVanillaPricingRequest request = hullFxRequest();
  request.tree.dt_years = 1 / 1000.;
  const auto result = priceFxVanilla(request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_NEAR(result->bsm_price, result->price, 0.0001);
}

TEST(PricingApiTest, FxPutCallParity) {
  FxVanillaPricingRequest request = hullFxRequest();
  // Sloped curves, so that each step drifts differently.
  request.tree.domestic = {.maturities = {0.1, 0.25}, .rates = {0.02, 0.06}};
  request.tree.foreign = {.maturities = {0.1, 0.25}, .rates = {0.08, 0.03}};
  const auto call = priceFxVanilla(request);
  request.option.is_call = false;
  const auto put = priceFxVanilla(request);
  ASSERT_TRUE(call.ok()) << call.status();
  ASSERT_TRUE(put.ok()) << put.status();

  const double df_dom = std::exp(-0.06 * 0.25);
  const double df_for = std::exp(-0.03 * 0.25);
  EXPECT_NEAR(call->price - put->price, 0.61 * df_for - 0.6 * df_dom, 1e-12);
}

TEST(PricingApiTest, FxWithZeroForeignRateMatchesPriceVanilla) {
  FxVanillaPricingRequest fx_request = hullFxRequest();
  fx_request.tree.foreign.rates = {0.0};
  fx_request.option.is_american = true;
  const auto fx = priceFxVanilla(fx_request);

  VanillaPricingRequest request;
  request.tree = {.model = TreeModel::kCoxRossRubinstein,
                  .spot = 0.61,
                  .vol = {.vol = 0.12},
                  .rate = 0.05,
                  .dt_years = 1 / 12.};
  request.option = fx_request.option;
  const auto equity = priceVanilla(request);

  ASSERT_TRUE(fx.ok()) << fx.status();
  ASSERT_TRUE(equity.ok()) << equity.status();
  EXPECT_NEAR(equity->price, fx->price, 1e-12);
  EXPECT_NEAR(equity->bsm_price, fx->bsm_price, 1e-12);
}

TEST(PricingApiTest, ForwardGrowthGivesUpProbs) {
  const auto result = priceFxVanilla(hullFxRequest());
  ASSERT_TRUE(result.ok()) << result.status();
  const int T = result->expiry_index;
  ASSERT_EQ(result->forward_growth.size(), static_cast<size_t>(T + 1));
  EXPECT_TRUE(std::isnan(result->forward_growth[T]));
  for (int t = 0; t < T; ++t) {
    const double dt = result->times[t + 1] - result->times[t];
    EXPECT_NEAR(std::exp((0.05 - 0.07) * dt), result->forward_growth[t], 1e-12);
    for (int i = 0; i <= t; ++i) {
      const double s = result->asset_values[nodeIndex(t, i)];
      const double up = result->asset_values[nodeIndex(t + 1, i + 1)] / s;
      const double down = result->asset_values[nodeIndex(t + 1, i)] / s;
      EXPECT_NEAR((result->forward_growth[t] - down) / (up - down),
                  result->up_probs[nodeIndex(t, i)],
                  1e-12);
    }
  }

  // For a single currency, it's the inverse of the forward discount factor.
  const auto equity = priceVanilla(exampleRequest());
  ASSERT_TRUE(equity.ok()) << equity.status();
  EXPECT_NEAR(1 / equity->forward_dfs[0], equity->forward_growth[0], 1e-12);
}

TEST(PricingApiTest, EvaluateCurve) {
  const RateCurveSpec curve{.maturities = {1, 2}, .rates = {0.02, 0.03}};
  const auto dfs = evaluateCurve(curve, {0, 0.5, 1, 1.5, 2, 3});
  ASSERT_TRUE(dfs.ok()) << dfs.status();
  // The 1y-2y forward is 4%, and continues past 2y.
  EXPECT_THAT(*dfs,
              testing::Pointwise(testing::DoubleNear(1e-12),
                                 std::vector<double>{1,
                                                     std::exp(-0.01),
                                                     std::exp(-0.02),
                                                     std::exp(-0.02 - 0.02),
                                                     std::exp(-0.06),
                                                     std::exp(-0.10)}));
}

TEST(PricingApiTest, RejectsInvalidFxRequests) {
  FxVanillaPricingRequest request = hullFxRequest();
  request.tree.foreign.maturities = {};
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceFxVanilla(request).status().code());

  request = hullFxRequest();
  request.tree.domestic = {.maturities = {2, 1}, .rates = {0.01, 0.02}};
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceFxVanilla(request).status().code());

  request = hullFxRequest();
  request.tree.vol = 0;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceFxVanilla(request).status().code());

  // A rate gap too wide for the vol and timestep admits arbitrage.
  request = hullFxRequest();
  request.tree.domestic.rates = {0.9};
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceFxVanilla(request).status().code());

  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            evaluateCurve({.maturities = {1}, .rates = {0.01}}, {-1})
                .status()
                .code());
}

// Every request is validated before pricing, with an error that names the
// problem (the essays show these messages to readers).
void expectInvalid(const absl::Status& status, std::string_view message) {
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code()) << status;
  EXPECT_THAT(status.message(), testing::HasSubstr(message));
}

TEST(PricingApiTest, ExplainsInvalidVanillaRequests) {
  const auto with = [](auto modify) {
    VanillaPricingRequest request = exampleRequest();
    modify(request);
    return priceVanilla(request).status();
  };
  using R = VanillaPricingRequest;
  expectInvalid(with([](R& r) { r.tree.spot = 0; }), "spot");
  expectInvalid(with([](R& r) { r.tree.rate = NAN; }), "finite");
  expectInvalid(with([](R& r) { r.tree.dt_years = 0; }), "dt_years");
  expectInvalid(with([](R& r) { r.option.strike = 0; }), "strike");
  expectInvalid(with([](R& r) {
                  r.tree.vol = {.model = VolModel::kTermStructure,
                                .term_maturities = {1, 2},
                                .term_vols = {0.2}};
                }),
                "equal length");
  expectInvalid(with([](R& r) {
                  r.tree.vol = {.model = VolModel::kTermStructure,
                                .term_maturities = {1, 2},
                                .term_vols = {0.2, -0.2}};
                }),
                "term_vols must be positive");
  expectInvalid(with([](R& r) {
                  r.tree.vol = {.model = VolModel::kTermStructure,
                                .term_maturities = {2, 1},
                                .term_vols = {0.2, 0.2}};
                }),
                "strictly increasing");
  expectInvalid(with([](R& r) {
                  r.tree.model = TreeModel::kLocalVolatility;
                  r.tree.vol = {.model = VolModel::kSigmoidSmile,
                                .vol = 0.2,
                                .smile_range = -0.1};
                }),
                "non-negative");
  // Enums arrive from JS as plain numbers, so out-of-range values are
  // possible.
  expectInvalid(with([](R& r) { r.tree.vol.model = static_cast<VolModel>(7); }),
                "unknown vol model");
  expectInvalid(with([](R& r) { r.tree.model = static_cast<TreeModel>(7); }),
                "unknown tree model");
}

TEST(PricingApiTest, ExplainsInvalidFxRequests) {
  const auto with = [](auto modify) {
    FxVanillaPricingRequest request = hullFxRequest();
    modify(request);
    return priceFxVanilla(request).status();
  };
  using R = FxVanillaPricingRequest;
  expectInvalid(with([](R& r) { r.tree.spot = 0; }), "spot");
  expectInvalid(with([](R& r) { r.tree.dt_years = 0; }), "dt_years");
  expectInvalid(with([](R& r) { r.option.strike = 0; }), "strike");
  expectInvalid(with([](R& r) { r.option.expiry_years = 0; }), "expiry_years");
  expectInvalid(with([](R& r) { r.tree.foreign.rates = {NAN}; }),
                "foreign curve: rates must be finite");
}

TEST(PricingApiTest, ExplainsOtherInvalidInputs) {
  expectInvalid(
      evaluateCurve({.maturities = {1, 2}, .rates = {0.01}}, {1}).status(),
      "equal length");
  expectInvalid(evaluateVol({.vol = 0}, 100, {1}).status(), "vol");
  expectInvalid(impliedVolatility({.price = 5, .spot = 0}).status(), "spot");
}

TEST(PricingApiTest, EvaluatesFlatVol) {
  const auto vols = evaluateVol({.vol = 0.2}, 100, {0.5, 1, 90, 110});
  ASSERT_TRUE(vols.ok()) << vols.status();
  EXPECT_THAT(*vols, testing::Each(0.2));
}

}  // namespace
}  // namespace smileexplorer::api
