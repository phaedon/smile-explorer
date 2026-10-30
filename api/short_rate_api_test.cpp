#include "api/short_rate_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

#include "absl/status/status.h"

namespace smileexplorer::api {
namespace {

using ::testing::DoubleNear;
using ::testing::ElementsAre;

// The example in Hull, "Options, Futures...", section 32.6 (as in
// ShortRateTreeCurveTest.Hull_SecondStage).
HullWhiteTreeSpec hullSpec() {
  return {.mean_reversion = 0.1,
          .sigma = 0.01,
          .dt_years = 1.0,
          .horizon_years = 3.0,
          .curve = {.maturities = {0.5, 1.0, 1.5, 2.0, 2.5, 3.0},
                    .rates = {.0343, .03824, .04183, .04512, .04812, .05086}}};
}

TEST(ShortRateApiTest, MatchesHull) {
  const auto result = buildHullWhiteTree(hullSpec());
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(2, result->j_max);
  EXPECT_NEAR(0.01 * std::sqrt(3.0), result->dr, 1e-15);
  EXPECT_THAT(result->times, ElementsAre(0, 1, 2, 3));
  EXPECT_THAT(
      std::vector<double>(result->alphas.begin(), result->alphas.begin() + 3),
      ElementsAre(DoubleNear(0.03824, 1e-5),
                  DoubleNear(0.05205, 1e-5),
                  DoubleNear(0.06252, 1e-5)));
  // Slices of 1, 3, 5 and then (clamped) 5 states.
  EXPECT_THAT(result->slice_offsets, ElementsAre(0, 1, 4, 9, 14));
}

TEST(ShortRateApiTest, FitsTheMarketCurve) {
  HullWhiteTreeSpec spec = hullSpec();
  spec.dt_years = 1 / 12.;
  spec.horizon_years = 5;
  const auto result = buildHullWhiteTree(spec);
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(61, std::ssize(result->times));
  for (size_t t = 0; t < result->times.size(); ++t) {
    EXPECT_NEAR(result->market_dfs[t], result->tree_dfs[t], 1e-12) << t;
  }
  EXPECT_NEAR(result->market_bond_price, result->bond_price, 1e-9);
}

TEST(ShortRateApiTest, BondPriceDoesNotDependOnTheModel) {
  HullWhiteTreeSpec spec = hullSpec();
  spec.dt_years = 0.25;
  const auto base = buildHullWhiteTree(spec);
  spec.mean_reversion = 0.5;
  spec.sigma = 0.03;
  const auto other = buildHullWhiteTree(spec);
  ASSERT_TRUE(base.ok()) << base.status();
  ASSERT_TRUE(other.ok()) << other.status();
  EXPECT_NEAR(base->bond_price, other->bond_price, 1e-9);
}

TEST(ShortRateApiTest, NodesAreConsistent) {
  HullWhiteTreeSpec spec = hullSpec();
  spec.dt_years = 0.1;
  const auto result = buildHullWhiteTree(spec);
  ASSERT_TRUE(result.ok()) << result.status();
  const auto& r = *result;
  const int num_slices = r.times.size();
  ASSERT_EQ(31, num_slices);
  ASSERT_EQ(num_slices + 1, std::ssize(r.slice_offsets));
  const int num_nodes = r.slice_offsets.back();
  ASSERT_EQ(num_nodes, std::ssize(r.short_rates));
  ASSERT_EQ(num_nodes, std::ssize(r.successor_mid));

  for (int t = 0; t < num_slices; ++t) {
    for (int n = r.slice_offsets[t]; n < r.slice_offsets[t + 1]; ++n) {
      EXPECT_LE(std::abs(r.j[n]), r.j_max);
      EXPECT_NEAR(r.j[n] * r.dr, r.x[n], 1e-12);
      EXPECT_NEAR(r.x[n] + r.alphas[t], r.short_rates[n], 1e-12);
      EXPECT_NEAR(1.0, r.pu[n] + r.pm[n] + r.pd[n], 1e-12);
      EXPECT_GT(r.pu[n], 0);
      EXPECT_GT(r.pm[n], 0);
      EXPECT_GT(r.pd[n], 0);
      if (t + 1 == num_slices) {
        EXPECT_EQ(-1, r.successor_mid[n]);
        continue;
      }
      // All three successors lie in the next slice.
      EXPECT_GT(r.successor_mid[n] - 1, r.slice_offsets[t + 1] - 1);
      EXPECT_LT(r.successor_mid[n] + 1, r.slice_offsets[t + 2]);
      // Branching bends inwards at the edges.
      const int mid_j = r.j[r.successor_mid[n]];
      if (r.branch_style[n] == 1) EXPECT_EQ(r.j[n] + 1, mid_j);
      if (r.branch_style[n] == 2) EXPECT_EQ(r.j[n] - 1, mid_j);
      if (r.branch_style[n] == 0) EXPECT_EQ(r.j[n], mid_j);
    }
  }

  // Each node's bond value is its discounted expected value one step later.
  const int n = r.slice_offsets[10] + 3;
  const int m = r.successor_mid[n];
  EXPECT_NEAR(std::exp(-r.short_rates[n] * 0.1) *
                  (r.pu[n] * r.bond_values[m + 1] + r.pm[n] * r.bond_values[m] +
                   r.pd[n] * r.bond_values[m - 1]),
              r.bond_values[n],
              1e-9);
}

TEST(ShortRateApiTest, RejectsInvalidSpecs) {
  HullWhiteTreeSpec spec = hullSpec();
  spec.mean_reversion = 0;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            buildHullWhiteTree(spec).status().code());

  spec = hullSpec();
  spec.curve.rates.pop_back();
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            buildHullWhiteTree(spec).status().code());

  spec = hullSpec();
  spec.dt_years = 0.001;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            buildHullWhiteTree(spec).status().code());

  // So slow that j_max would overflow an int.
  spec = hullSpec();
  spec.mean_reversion = 1e-12;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            buildHullWhiteTree(spec).status().code());

  spec = hullSpec();
  spec.horizon_years = 1e300;
  spec.dt_years = 1e-300;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            buildHullWhiteTree(spec).status().code());
}

// Hull, example 32.1: a 3-year put, struck at 63, on a 9-year zero-coupon bond.
// (Hull interpolates zero rates differently, hence the tolerances.)
BondOptionRequest hullBondOption() {
  std::vector<double> maturities{3,
                                 31,
                                 62,
                                 94,
                                 185,
                                 367,
                                 731,
                                 1096,
                                 1461,
                                 1826,
                                 2194,
                                 2558,
                                 2922,
                                 3287,
                                 3653};
  for (auto& m : maturities) m /= 365.;
  return {.tree = {.mean_reversion = 0.1,
                   .sigma = 0.01,
                   .dt_years = 3 / 50.,
                   .horizon_years = 9,
                   .curve = {.maturities = maturities,
                             .rates = {.0501772,
                                       .0498284,
                                       .0497234,
                                       .0496157,
                                       .0499058,
                                       .0509389,
                                       .0579733,
                                       .0630595,
                                       .0673464,
                                       .0694816,
                                       .0708807,
                                       .0727527,
                                       .0730852,
                                       .073979,
                                       .0749015}},
                   .bond_face = 100},
          .option = {.strike = 63, .expiry_years = 3, .is_call = false}};
}

TEST(ShortRateApiTest, BondOptionMatchesHull) {
  const auto result = priceBondOption(hullBondOption());
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(50, result->expiry_index);
  EXPECT_NEAR(1.8057, result->price, 0.01);
  EXPECT_NEAR(1.8093, result->analytic_price, 0.01);
  EXPECT_NEAR(result->analytic_price, result->price, 0.01);
  EXPECT_DOUBLE_EQ(result->price, result->european_price);
  EXPECT_DOUBLE_EQ(result->price, result->option_values[0]);
}

TEST(ShortRateApiTest, AmericanBondOption) {
  BondOptionRequest request = hullBondOption();
  request.tree.dt_years = 0.25;
  request.option.is_american = true;
  const auto result = priceBondOption(request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_GT(result->price, result->european_price);

  const auto& nodes = result->tree;
  for (int t = 0; t <= result->expiry_index; ++t) {
    for (int n = nodes.slice_offsets[t]; n < nodes.slice_offsets[t + 1]; ++n) {
      EXPECT_NEAR(std::max(0.0, 63 - nodes.bond_values[n]),
                  result->exercise_values[n],
                  1e-12);
      if (t < result->expiry_index) {
        EXPECT_NEAR(std::max(result->exercise_values[n],
                             result->continuation_values[n]),
                    result->option_values[n],
                    1e-9);
      } else {
        EXPECT_TRUE(std::isnan(result->continuation_values[n]));
      }
    }
  }
  // After expiry, there are no option values.
  EXPECT_TRUE(std::isnan(result->option_values.back()));
}

TEST(ShortRateApiTest, RejectsInvalidBondOptions) {
  BondOptionRequest request = hullBondOption();
  request.option.expiry_years = 9;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceBondOption(request).status().code());
  request.option.expiry_years = 0.01;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceBondOption(request).status().code());
  request = hullBondOption();
  request.option.strike = 0;
  EXPECT_EQ(absl::StatusCode::kInvalidArgument,
            priceBondOption(request).status().code());
}

void expectInvalid(const absl::Status& status, std::string_view message) {
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.code()) << status;
  EXPECT_THAT(status.message(), testing::HasSubstr(message));
}

TEST(ShortRateApiTest, ExplainsInvalidSpecs) {
  const auto with = [](auto modify) {
    HullWhiteTreeSpec spec = hullSpec();
    modify(spec);
    return buildHullWhiteTree(spec).status();
  };
  using S = HullWhiteTreeSpec;
  expectInvalid(with([](S& s) { s.sigma = 0; }), "sigma");
  expectInvalid(with([](S& s) { s.dt_years = 0; }), "dt_years");
  expectInvalid(with([](S& s) { s.horizon_years = -1; }), "horizon_years");
  expectInvalid(with([](S& s) { s.bond_face = 0; }), "bond_face");
  // Slow mean reversion keeps the tree widening: 500 steps would need about
  // 250,000 nodes.
  expectInvalid(with([](S& s) {
                  s.mean_reversion = 0.0005;
                  s.dt_years = 0.01;
                  s.horizon_years = 5;
                }),
                "nodes");
}

TEST(ShortRateApiTest, ExplainsInvalidBondOptions) {
  BondOptionRequest request = hullBondOption();
  request.option.expiry_years = 0;
  expectInvalid(priceBondOption(request).status(), "expiry_years");

  request = hullBondOption();
  request.tree.sigma = 0;
  expectInvalid(priceBondOption(request).status(), "sigma");
}

}  // namespace
}  // namespace smileexplorer::api
