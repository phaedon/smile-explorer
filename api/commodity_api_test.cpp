#include "api/commodity_api.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <numbers>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "commodities/commodity_tree.h"

namespace smileexplorer::api {
namespace {

// Twelve monthly contracts on a seasonal curve that peaks in winter.
CommoditySpec gas() {
  CommoditySpec spec{.spot = 3.0,
                     .rate = 0.04,
                     .mean_reversion = 1.5,
                     .sigma = 0.6,
                     .steps_per_month = 4,
                     .horizon_years = 1.0};
  for (int m = 1; m <= 12; ++m) {
    const double t = m / 12.0;
    spec.futures_times.push_back(t);
    spec.futures_prices.push_back(
        3.0 * (1 + 0.25 * std::cos(2 * std::numbers::pi * (t - 2.0 / 12))));
  }
  return spec;
}

TEST(CommodityApiTest, TreeIsFittedToTheCurve) {
  const auto result = buildCommodityTree(gas());
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->times.size(), 49u);
  EXPECT_DOUBLE_EQ(result->times.back(), 1.0);
  for (size_t t = 0; t < result->times.size(); ++t) {
    EXPECT_NEAR(result->expected_spots[t] / result->curve_prices[t], 1, 1e-12);
  }
  // Each pillar's price is on the curve.
  EXPECT_NEAR(result->curve_prices[12], gas().futures_prices[2], 1e-12);
  EXPECT_EQ(result->j_max, 6);
  EXPECT_DOUBLE_EQ(result->dx, 0.6 * std::sqrt(3.0 / 48));
}

TEST(CommodityApiTest, NodesAreLaidOutSliceBySlice) {
  const auto result = buildCommodityTree(gas());
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->slice_offsets.size(), result->times.size() + 1);
  const int num_nodes = result->slice_offsets.back();
  EXPECT_EQ(result->spots.size(), static_cast<size_t>(num_nodes));
  EXPECT_EQ(result->successor_mid.size(), static_cast<size_t>(num_nodes));
  // 1, 3, 5, ..., 13, then 13 a slice.
  EXPECT_EQ(result->slice_offsets[1], 1);
  EXPECT_EQ(result->slice_offsets[2], 4);
  EXPECT_EQ(result->slice_offsets.back() - result->slice_offsets[48], 13);
  EXPECT_EQ(result->successor_mid[0], 2);
  EXPECT_EQ(result->successor_mid.back(), -1);
  // The edges bend inwards.
  EXPECT_EQ(result->branch_style[result->slice_offsets[10]], 1);
  EXPECT_EQ(result->branch_style[result->slice_offsets[11] - 1], 2);
  // Within a slice, prices go up, and the probabilities add to one.
  double total = 0;
  for (int n = result->slice_offsets[20]; n < result->slice_offsets[21]; ++n) {
    if (n > result->slice_offsets[20]) {
      EXPECT_GT(result->spots[n], result->spots[n - 1]);
    }
    EXPECT_NEAR(result->pu[n] + result->pm[n] + result->pd[n], 1, 1e-12);
    total += result->probabilities[n];
  }
  EXPECT_NEAR(total, 1, 1e-12);
  EXPECT_EQ(result->j[result->slice_offsets[20]], -6);
  EXPECT_DOUBLE_EQ(result->x[result->slice_offsets[20]], -6 * result->dx);
}

TEST(CommodityApiTest, MeanReversionNarrowsTheConeAgainstGbm) {
  const auto result = buildCommodityTree(gas());
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_DOUBLE_EQ(result->spot_q05[0], 3.0);
  EXPECT_DOUBLE_EQ(result->gbm_q95[0], 3.0);
  const size_t last = result->times.size() - 1;
  EXPECT_LT(result->spot_q95[last], result->gbm_q95[last]);
  EXPECT_GT(result->spot_q05[last], result->gbm_q05[last]);
  EXPECT_LT(result->spot_q50[last], result->curve_prices[last]);
  EXPECT_LT(result->gbm_q50[last], result->spot_q50[last]);
  // The width of the cone (relative to the curve) stops growing.
  const auto width = [&](size_t t) {
    return std::log(result->spot_q95[t] / result->spot_q05[t]);
  };
  EXPECT_LT(width(48) - width(24), width(24) - width(0));
}

TEST(CommodityApiTest, VolsShowTheSamuelsonEffect) {
  const auto result = buildCommodityTree(gas());
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(result->futures_vols.size(), 12u);
  for (size_t k = 1; k < 12; ++k) {
    EXPECT_LT(result->futures_vols[k], result->futures_vols[k - 1]);
    EXPECT_LT(result->option_vols[k], result->option_vols[k - 1]);
    // An option's vol averages over the futures' (higher) vols before
    // delivery.
    EXPECT_GT(result->option_vols[k], result->futures_vols[k]);
  }
  EXPECT_NEAR(result->implied_carry[11],
              std::log(gas().futures_prices[11] / 3.0),
              1e-12);
}

TEST(CommodityApiTest, OptionMatchesTheLibraryAndBlack76) {
  CommodityOptionRequest request{.commodity = gas(),
                                 .option = {.delivery_years = 3 / 12.0,
                                            .expiry_years = 2 / 12.0,
                                            .strike = 3.6,
                                            .is_call = true,
                                            .is_american = true}};
  const auto result = priceCommodityOption(request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->delivery_index, 12);
  EXPECT_EQ(result->expiry_index, 8);
  EXPECT_NEAR(result->futures_price, gas().futures_prices[2], 1e-12);
  EXPECT_DOUBLE_EQ(result->implied_vol,
                   samuelsonImpliedVol(1.5, 0.6, 2 / 12.0, 3 / 12.0));
  EXPECT_NEAR(result->european_price / result->black_price, 1, 0.02);
  EXPECT_GE(result->price, result->european_price);

  // Per node, NaN after expiry.
  const auto tree = buildCommodityTree(gas());
  ASSERT_TRUE(tree.ok());
  const int num_nodes = tree->slice_offsets.back();
  ASSERT_EQ(result->option_values.size(), static_cast<size_t>(num_nodes));
  EXPECT_DOUBLE_EQ(result->option_values[0], result->price);
  EXPECT_TRUE(std::isnan(result->option_values[tree->slice_offsets[9]]));
  EXPECT_TRUE(std::isnan(result->futures_values.back()));
  EXPECT_EQ(result->exercised.back(), 0);
  // At expiry, the top node is in the money.
  EXPECT_EQ(result->exercised[tree->slice_offsets[9] - 1], 1);
  EXPECT_DOUBLE_EQ(result->option_values[tree->slice_offsets[9] - 1],
                   result->futures_values[tree->slice_offsets[9] - 1] - 3.6);
}

TEST(CommodityApiTest, PutUsesBlack76Put) {
  CommodityOptionRequest request{.commodity = gas(),
                                 .option = {.delivery_years = 0.5,
                                            .expiry_years = 0.5,
                                            .strike = 2.5,
                                            .is_call = false}};
  const auto result = priceCommodityOption(request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_DOUBLE_EQ(result->price, result->european_price);
  EXPECT_NEAR(result->european_price / result->black_price, 1, 0.02);
}

void expectInvalid(const std::function<void(CommodityOptionRequest&)>& change,
                   std::string_view message) {
  CommodityOptionRequest request{.commodity = gas()};
  change(request);
  const auto result = priceCommodityOption(request);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument)
      << message;
  EXPECT_NE(result.status().message().find(message), std::string_view::npos)
      << result.status();
  // Everything but the option's own fields is checked when building the
  // tree too.
  if (message != "strike" && message != "delivery_years" &&
      message != "expiry_years") {
    EXPECT_EQ(buildCommodityTree(request.commodity).status().code(),
              absl::StatusCode::kInvalidArgument)
        << message;
  }
}

TEST(CommodityApiTest, RejectsBadInputs) {
  expectInvalid([](auto& r) { r.commodity.spot = 0; }, "spot");
  expectInvalid([](auto& r) { r.commodity.futures_times.clear(); }, "pillars");
  expectInvalid([](auto& r) { r.commodity.futures_prices.pop_back(); },
                "pillars");
  expectInvalid(
      [](auto& r) {
        r.commodity.futures_times.assign(61, 0.1);
        r.commodity.futures_prices.assign(61, 3);
      },
      "pillars");
  expectInvalid([](auto& r) { r.commodity.futures_times[3] = 0.1; },
                "increasing");
  expectInvalid([](auto& r) { r.commodity.futures_times[0] = 0; },
                "increasing");
  expectInvalid([](auto& r) { r.commodity.futures_prices[2] = -1; },
                "futures prices");
  expectInvalid([](auto& r) { r.commodity.rate = 0.6; }, "rate");
  expectInvalid([](auto& r) { r.commodity.mean_reversion = 0; },
                "mean_reversion");
  expectInvalid([](auto& r) { r.commodity.sigma = 0; }, "sigma");
  expectInvalid([](auto& r) { r.commodity.steps_per_month = 0; },
                "steps_per_month");
  expectInvalid([](auto& r) { r.commodity.horizon_years = 4; },
                "horizon_years");
  expectInvalid(
      [](auto& r) {
        r.commodity.mean_reversion = 1e-3;
        r.commodity.horizon_years = 3;
        r.commodity.steps_per_month = 16;
      },
      "nodes");
  expectInvalid([](auto& r) { r.option.strike = 0; }, "strike");
  expectInvalid([](auto& r) { r.option.delivery_years = 1.5; },
                "delivery_years");
  expectInvalid([](auto& r) { r.option.delivery_years = 0.1; },
                "delivery_years");
  expectInvalid([](auto& r) { r.option.expiry_years = 0.5; }, "expiry_years");
  expectInvalid([](auto& r) { r.option.expiry_years = 0; }, "expiry_years");
}

}  // namespace
}  // namespace smileexplorer::api
