#include "api/short_rate_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
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

}  // namespace
}  // namespace smileexplorer::api
